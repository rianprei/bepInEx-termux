# u_noads — adapters por SDK (como cada fechamento funciona de verdade)

Regra: suprimir o `Show` e disparar o MESMO caminho que o SDK usa ao fechar
(delegates/eventos atuais lidos na hora, listener passado pelo jogo). Se o
fechamento falhar, o fake REPASSA pro original (o anúncio aparece; o jogo
nunca trava). Rewarded fica de fora por escopo em todos.

Thread: o fechamento roda na thread que chamou o Show (na prática, a main do
Unity — é lá que jogo chama Show), com `thread_attach` antes de qualquer
`runtime_invoke`. É o mesmo contexto em que o próprio SDK dispararia.

## GoogleMobileAds (InterstitialAd, AppOpenAd)

- Alvo: `GoogleMobileAds.Api.InterstitialAd.Show()` (0 args, void),
  `GoogleMobileAds.Api.AppOpenAd.Show()` (0 args, void).
- Fecha: eventos C# `OnAdClosed` (plugin v6) / `OnAdDidDismissFullScreenContent`
  (plugin v7+), ambos `EventHandler<EventArgs>`. Lê o delegate atual do
  backing field do objeto e invoca com `(sender, EventArgs novo)`.
- Fonte: https://github.com/googleads/googleads-mobile-unity
  - `source/plugin/Assets/GoogleMobileAds/Api/InterstitialAd.cs`
    (`public event EventHandler<EventArgs> OnAdClosed;`, `public void Show()`)
  - `source/plugin/Assets/GoogleMobileAds/Api/AppOpenAd.cs`
    (`OnAdDidDismissFullScreenContent`, `Show()`)
- Risco residual: nenhum — EventArgs é construído pelo mod, sem dado fabricado.

## UnityAds 4.x (Advertisement)

- Alvo: `UnityEngine.Advertisements.Advertisement.Show(adUnitId, listener)`
  (2 args) e `Show(adUnitId, options, listener)` (3 args), estáticos, void.
  Listener é sempre o ÚLTIMO arg.
- Fecha: `IUnityAdsShowListener.OnUnityAdsShowComplete(listener, adUnitId,
  COMPLETED)` — o listener É o objeto do jogo (veio no Show), o adUnitId é
  reaproveitado do arg0, e COMPLETED é lido como enum boxeado do campo
  estático (sem chutar valor numérico). Sem enum no jogo = não suprime.
- Fonte: https://docs.unity.com/en-us/monetization/sdk-integration/unity-sdk/api/unity-api
  (Advertisement.Show; interface IUnityAdsShowListener; enum
  UnityAdsShowCompletionState). Docs do rewarded confirmam o overload de 2
  args: https://docs.unity.com/en-us/ads-unity/4.20.0/sdk-integration/rewarded-ads
- Risco residual: nenhum — todos os valores vêm do jogo/SDK.

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

- Alvo: `MaxSdk.ShowInterstitial(adUnitId, placement, customData)` e
  `MaxSdk.ShowAppOpenAd(...)` (3 args, estáticos, void).
- Fecha: campo estático internal `onAdHiddenEvent`
  (`Action<string, AdInfo>`) em `MaxSdkCallbacks.Interstitial` /
  `MaxSdkCallbacks.AppOpen`, invocado com `(adUnitId do Show, null)`.
- Fonte: https://github.com/AppLovin/AppLovin-MAX-Unity-Plugin
  - `DemoApp/Assets/MaxSdk/Scripts/MaxSdkAndroid.cs` (`ShowInterstitial`,
    `ShowAppOpenAd` com 3 args)
  - `DemoApp/Assets/MaxSdk/Scripts/MaxSdkCallbacks.cs` (`internal static
    Action<string, MaxSdkBase.AdInfo> onAdHiddenEvent` + evento público)
- Risco residual: `AdInfo` null (mesma regra do LevelPlay: exceção vira
  repasse). Classe `MaxSdk` sem namespace é suportada pelo split.

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
  `class_get_nested_types`), invocado com `MeticaAd` vazio (`object_new`
  sem ctor) ou null se a classe sumir.
- Fonte: dump do próprio jogo (`F .../Interstitial OnAdHidden
  System.Action<Metica.Ads.MeticaAd> 1 32`,
  `M ... ShowInterstitial 3 System.Void 0`). SDK proprietário, sem fonte
  pública — ground truth é o C5 do jogo.
- Risco residual: handler que lê campo do MeticaAd vazio pega null (não
  crasha por si); null direto se `object_new` falhar.

## Manager customizado do jogo: FORA do u_noads

`InterstitialAdManager.TryShowInterstitial` (SA2) é código do jogo, não SDK:
não entra no genérico. Vira `.patch` documentado — ver
`examples/sa2-interstitial.patch` (equivalente ao hook do sa2content, que
faz o método virar no-op).
