# u_noads — adapters por SDK (como cada fechamento funciona de verdade)

Regra: suprimir o `Show` e disparar o MESMO caminho que o SDK usa ao fechar
(delegates/eventos atuais lidos na hora, listener passado pelo jogo). Se o
fechamento **não** acontecer, o fake REPASSA pro original (o anúncio aparece; o
jogo nunca trava). Rewarded fica de fora por escopo em todos.

**NUNCA suprimir sem fechar** (regra da revisão): se não achamos o callback do
jogo, ou o invoke falha, o anúncio volta a aparecer. Ad que some sem o evento de
fechamento deixa o fluxo do jogo esperando para sempre (loading infinito), o que
é pior que o anúncio. O motivo da falha vai para o log (campo `note` do ctx,
impresso no `log.txt`).

Thread: o fechamento roda na thread que chamou o Show (na prática, a main do
Unity — é lá que jogo chama Show), com `thread_attach` antes de qualquer
`runtime_invoke`. É o mesmo contexto em que o próprio SDK dispararia.

## GoogleMobileAds (InterstitialAd, AppOpenAd)

- Alvo: `GoogleMobileAds.Api.InterstitialAd.Show()` (0 args, void),
  `GoogleMobileAds.Api.AppOpenAd.Show()` (0 args, void).
- Fecha, por plugin: `OnAdClosed` (v6) é `EventHandler<EventArgs>` → `Invoke`
  com **2** args; `OnAdFullScreenContentClosed` (v7+) é `Action` → `Invoke` com
  **0** args. Lê o delegate atual do backing field do objeto e invoca.
  `OnAdDidDismissFullScreenContent` NÃO é campo do `InterstitialAd`: é evento da
  ponte interna (`_client`), que o jogo não enxerga — por isso a revisão apontou
  que ele nunca fechava nada.
- Fonte: https://github.com/googleads/googleads-mobile-unity/tree/v11.5.0
  `source/plugin/Assets/GoogleMobileAds/Api/InterstitialAd.cs` (linha 54,
  fonte externo)
  (`public event Action OnAdFullScreenContentClosed;`), `:229-231` (é ele que
  dispara no close) e `:175` (`public void Show()`); v6 no histórico do
  repositório principal `googleads-mobile-unity`, em
  `source/plugin/Assets/GoogleMobileAds/Api/InterstitialAd.cs`
  (`public event EventHandler<EventArgs> OnAdClosed;`).
- Risco residual: plugin v6/v7 não reconhecido → **não suprime** (regra acima) e
  o log aponta o que faltou.

## UnityAds 4.x (Advertisement)

- Alvo: `UnityEngine.Advertisements.Advertisement.Show(adUnitId, listener)`
  (2 args) e `Show(adUnitId, options, listener)` (3 args), estáticos, void.
  Listener é sempre o ÚLTIMO arg.
- Fecha: `IUnityAdsShowListener.OnUnityAdsShowComplete(adUnitId, COMPLETED)` — o
  listener É o objeto do jogo (veio no Show), o adUnitId é reaproveitado do
  arg0. O `COMPLETED` é um enum: o valor do const é lido cru (4 bytes) e
  **boxeado de verdade** (`object_new` + `value__`), porque
  `field_static_get_value` devolve o valor e não um objeto — passar o int como
  ponteiro do enum no `runtime_invoke` fazia o invoker desreferenciar endereço
  inválido e o jogo tomar SIGSEGV (achado da revisão, agora coberto por
  `test_closers.cpp`). Sem o enum (ou sem `value__`) = não suprime.
- Fonte: https://docs.unity.com/en-us/monetization/sdk-integration/unity-sdk/api/unity-api
  (Advertisement.Show; interface IUnityAdsShowListener; enum
  UnityAdsShowCompletionState). Docs do rewarded confirmam o overload de 2
  args: https://docs.unity.com/en-us/ads-unity/4.20.0/sdk-integration/rewarded-ads
- Risco residual: listener do jogo que levanta exceção → invoke devolve false →
  não suprime e o log diz "OnUnityAdsShowComplete levantou excecao no jogo".

## LevelPlay (LevelPlayInterstitialAd)

- Alvo: `ShowAd()` (0 args) e `ShowAd(placement)` (1 arg), void.
- Fecha: evento `OnAdClosed` (`Action<LevelPlayAdInfo>`), delegate atual lido
  do objeto, invocado com `null` (sem AdInfo fabricável).
- Fonte: https://docs.unity.com/en-us/grow/levelplay/sdk/unity/interstitial-integration
  (assinaturas ShowAd + lista de eventos com handler de 1 arg) e
  https://docs.unity.com/en-us/grow/levelplay/sdk/unity/migrate-interstitial-ad-unit-api
- Risco residual: handler do jogo que dereferencia `adInfo` dá NRE — vira
  exceção no invoke, o mod repassa pro original (anúncio aparece). Device
  decide; log mostra "fechamento falhou".

## AppLovin MAX (MaxSdk)

- Alvo: `MaxSdk.ShowInterstitial` e `MaxSdk.ShowAppOpenAd`, **1, 2 e 3 args**:
  a assinatura é `ShowInterstitial(string adUnitIdentifier, string placement =
  null, string customData = null)`: é um método C# com parâmetro opcional e
  continua um único método IL de 3 parâmetros. Os registros de 1, 2 e 3
  permanecem porque o plugin antigo `release_4_3_4` tinha overloads explícitos
  de 1 e 2 argumentos (a de 1 é a mais comum nesse plugin antigo).
- Fecha: campo estático internal `onAdHiddenEvent`
  (`Action<string, AdInfo>`) em `MaxSdkCallbacks.Interstitial` /
  `MaxSdkCallbacks.AppOpen`, invocado com `(args[0] do Show = adUnitIdentifier,
  null)`. Confirmado no dump do SA2: `F MaxSdkCallbacks/Interstitial
  onAdHiddenEvent System.Action<System.String,MaxSdkBase.AdInfo> 1 64`.
- Fonte (DEFINIÇÃO da API, não um chamador):
  https://github.com/AppLovin/AppLovin-MAX-Unity-Plugin
  `DemoApp/Assets/MaxSdk/Scripts/MaxSdkAndroid.cs` (linha 621, fonte externo)
  (`ShowInterstitial`) e
  `:698` (`ShowAppOpenAd`) — esse caminho `DemoApp/Assets/MaxSdk/Scripts/` é o
  plugin que a loja/instalador copia pro projeto, é a implementação da API.
  Callbacks em `.../MaxSdkCallbacks.cs`.
- Risco residual: `AdInfo` null (mesma regra do LevelPlay: exceção vira repasse,
  e aí **não** suprime). `MaxSdk` não existe no dump do SA2 — nesse jogo só
  Metica resolve.

## Meta Audience Network (AudienceNetwork.InterstitialAd)

- Alvo: `Show()` (0 args) que RETORNA bool.
- Fecha: NÃO dispara nada — suprime retornando `false`, o caminho legítimo
  "sem fill" do SDK (o jogo já trata). Disparar `DidClose` junto correria o
  risco de fluxo duplo (no-fill + closed).
- Fonte: dump do próprio jogo (SA2, `/tmp/sa2_dump.tsv` — o plugin AN é
  descontinuado, sem fonte oficial viva):
  `M AudienceNetwork.InterstitialAd Show 0 System.Boolean 0`,
  `get_InterstitialAdDidClose 0 ...FBInterstitialAdBridgeCallback 0`,
  `FBInterstitialAdBridgeCallback.Invoke 0 System.Void 0`.
- Risco residual: jogo que só continua via DidClose após Show()==true ficaria
  esperando — nesse caso o device mostra e o adapter muda pra disparo.

## Metica (SA2: Metica.Ads.MeticaAds/MeticaAdsImpl)

- Alvo: `ShowInterstitial` (3 args, void) nas duas classes.
- Fecha: evento estático `OnAdHidden` (`Action<MeticaAd>`) em
  `MeticaAdsCallbacks/Interstitial` (nested; resolvido por
  `class_get_nested_types`), invocado com um `MeticaAd` **construído**: o dump
  mostra `M Metica.Ads.MeticaAd .ctor 0 System.Void 0`, então o mod chama o ctor
  de 0 args depois do `object_new` (a revisão pegou que antes ia objeto cru, sem
  ctor). Se o ctor ou o handler levantarem, não suprime e o log traz a nota
  ("MeticaAd sem .ctor...", "OnAdHidden levantou excecao no jogo").
- Fonte: dump do próprio jogo (`F .../Interstitial OnAdHidden
  System.Action<Metica.Ads.MeticaAd> 1 32`,
  `M ... ShowInterstitial 3 System.Void 0`). SDK proprietário, sem fonte
  pública — ground truth é o C5 do jogo.
- Risco residual: handler que lê campo do MeticaAd vazio pega null (não
  crasha por si); null direto se `object_new` falhar.

## Manager customizado do jogo: FORA do u_noads

`InterstitialAdManager.TryShowInterstitial` (SA2) é código do jogo, não SDK:
não entra no genérico. Vira `.bpatch` documentado — ver
`examples/sa2-interstitial.bpatch` (equivalente ao hook do sa2content, que
faz o método virar no-op).
