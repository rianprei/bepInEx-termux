# DLL corpus: first-party evidence

## Scope and method

This ledger covers exactly the 30 corpus identifiers below. Evidence is limited
to project-maintained repositories, release/download pages, and license files.
No binaries were downloaded. “Public artifact” means a first-party page lists
or links an artifact; it does not identify a corpus sample by hash or version.
Project source, a release listing, and a sampled compiled DLL are not
interchangeable evidence.

**Eligibility rule:** eligible means the evidence identifies the sampled DLL as
the project artifact and the applicable license text expressly authorizes
studying or reverse-engineering that DLL. “Unresolved” is not a legal
prohibition. None of the located licenses expressly grants a reverse-
engineering right. MIT, BSD-2-Clause, and Apache-2.0 grant specified rights
over the licensed work (including certain use, reproduction, modification,
and/or object-form distribution); those terms do not by themselves establish
that an unidentified corpus DLL is that licensed work or grant blanket
permission to decompile it. Public download availability is reported
separately. Confidence refers to the project/source identification, not the
identity of the sampled build.

## The Planet Crafter — 12 entries

The project solution lists each named project below; its shared plugin uses
BepInEx `BaseUnityPlugin`, supporting BepInEx/Unity Mono for these project
builds. The repository has an Apache-2.0 license. The source license grants
rights over source and object-form works subject to its terms, but contains no
express reverse-engineering permission. The repository source is public; a
first-party DLL download matching any sample was not verified.
[Shared plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/CheatAutoConsume/Plugin.cs#L4-L17) ·
[solution](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/ThePlanetCrafterMods.sln) ·
[BepInEx plugin guide](https://docs.bepinex.dev/articles/dev_guide/plugin_tutorial/2_plugin_start.html) ·
[LICENSE](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE)

| Corpus entry (project evidence) | Game | Loader / backend | License / download permission | Eligibility | Confidence |
|---|---|---|---|---|---|
| `TPC_CheatAutoConsume` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/CheatAutoConsume/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_CheatAutoHarvest` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/CheatAutoHarvest/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_CheatCraftFromNearbyContainers` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/CheatCraftFromNearbyContainers/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_CheatInventoryStacking` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/CheatInventoryStacking/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_CheatMoreTrade` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/CheatMoreTrade/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_FeatFlatlands` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/FeatFlatlands/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_FeatSpaceCows` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/FeatSpaceCows/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_FixUnofficialPatches` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/FixUnofficialPatches/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_ItemRods` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/ItemRods/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_LathreyAutoMove` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LathreyAutoMove/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_UIHotbar` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/UIHotbar/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `TPC_UIQuickLoot` ([plugin](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/UIQuickLoot/Plugin.cs)) | The Planet Crafter | BepInEx / Mono | [Apache-2.0](https://github.com/akarnokd/ThePlanetCrafterMods/blob/b8f9440ac5da77bb4a4ee66177ccdee00d02b8d2/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |

## Dyson Sphere Program — 10 entries

The project solution lists the ten projects below. The TidalLocked plugin
extends BepInEx `BaseUnityPlugin` and references UnityEngine, supporting
BepInEx/Unity Mono for these project builds. The AddFuelStar first-party
README documents the BepInEx installation location. The repository has an
Apache-2.0 license with no express reverse-engineering permission. Its
release directories publicly list DLLs for the entries marked “listed”;
ChangeSun has no corresponding official release DLL verified here. Listing a
DLL does not establish that a corpus sample is the same build.
[Solution](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/DSPMod.sln) ·
[plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/TidalLocked/TidalLocked.cs#L7-L16) ·
[BepInEx install README](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/AddFuelStar-Release/README.md) ·
[BepInEx plugin guide](https://docs.bepinex.dev/articles/dev_guide/plugin_tutorial/2_plugin_start.html) ·
[LICENSE](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE)

| Corpus entry (project evidence) | Game | Loader / backend | License / public artifact | Eligibility | Confidence |
|---|---|---|---|---|---|
| `dsp-AddFuelStar` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/AddFuelStar/AddFuelStar.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/AddFuelStar-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-BiggerSeed` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/BiggerSeed/BiggerSeed.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/BiggerSeed-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-CanNotShowItem` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/CanNotShowItem/CanNotShowItem.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/CanNotShowItem-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-ChangeSun` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/ChangeSun/ChangeSun.cs)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); source project found, official DLL listing unverified; no express reverse-engineering grant | Unresolved: public binary and sample/build not verified | High project; low sample |
| `dsp-DSPHierarchicalDisplay` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/DSPHierarchicalDisplay/DSPHierarchicalDisplay.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/DSPHierarchicalDisplay-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-DSPModSave` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/DSPModSave/DSPModSave.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/DSPModSave-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-GeothermalUsing` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/GeothermalUsing/GeothermalUsing.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/GeothermalUsing-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-PlanetMiner` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/PlanetMiner/PlanetMiner.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/PlanetMiner-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-StationAutoCollect` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/StationAutoCollet/StationAutoCollect.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/StationAutoCollet-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `dsp-TidalLocked` ([plugin](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/TidalLocked/TidalLocked.cs); [release folder](https://github.com/crecheng/DSPMod/tree/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/TidalLocked-Release)) | Dyson Sphere Program | BepInEx / Mono | [Apache-2.0](https://github.com/crecheng/DSPMod/blob/9dabb0275a3bafa0fa1a992fda5d8e0c7f29ff8a/LICENSE); DLL listed publicly; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |

The StationAutoCollect release directory is spelled `StationAutoCollet-Release`
in the project repository; the spelling is preserved in the source link.

## Other projects — 8 entries

| Corpus entry (first-party project evidence) | Game | Loader / backend | License / download permission | Eligibility | Confidence |
|---|---|---|---|---|---|
| `Darkwood_Customizer` ([plugin](https://github.com/xamionex/DarkwoodCustomizer/blob/5583bff8b4beb9525e0782a93b99ac7b20e2b8f1/Plugin.cs#L15-L17); [project file](https://github.com/xamionex/DarkwoodCustomizer/blob/5583bff8b4beb9525e0782a93b99ac7b20e2b8f1/DarkwoodCustomizer.csproj#L4-L30)) | Darkwood | BepInEx / Mono | [MIT LICENSE](https://github.com/xamionex/DarkwoodCustomizer/blob/5583bff8b4beb9525e0782a93b99ac7b20e2b8f1/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | Unresolved: sample/build and license applicability unverified | High project; low sample |
| `FFPR_Fix` ([plugin](https://github.com/d3xMachina/FFPR-Fix/blob/2e98a03f4f45afda7b9b185d4a6be4bb12d75f36/Plugin.cs#L1-L16); [README / releases](https://github.com/d3xMachina/FFPR-Fix/blob/2e98a03f4f45afda7b9b185d4a6be4bb12d75f36/README.md#L3-L30)) | Final Fantasy Pixel Remaster | BepInEx / IL2CPP | [MIT LICENSE](https://github.com/d3xMachina/FFPR-Fix/blob/2e98a03f4f45afda7b9b185d4a6be4bb12d75f36/LICENSE.txt); README links official releases; no express reverse-engineering grant | Unresolved: release/sample identity and license applicability unverified | High project; medium sample |
| `Magicite` ([README](https://github.com/Silvris/Magicite/blob/30f0d5d0c040baca37e9657e4166c2651621bd95/README.md#L2-L5); [entry point](https://github.com/Silvris/Magicite/blob/30f0d5d0c040baca37e9657e4166c2651621bd95/Magicite/EntryPoint.cs#L1-L27)) | Final Fantasy Pixel Remaster | BepInEx / IL2CPP | [MIT LICENSE](https://github.com/Silvris/Magicite/blob/30f0d5d0c040baca37e9657e4166c2651621bd95/LICENSE); source public, first-party public DLL not verified; no express reverse-engineering grant | Unresolved: no matching public DLL/build or license applicability established | High project; low sample |
| `MiSide_KappiMod` ([README: loader variants and releases](https://github.com/MrSago/MiSide-KappiMod/blob/f01a0763e18a6b8337cc4a831cadbed173c73c35/README.md#L26-L31), [install](https://github.com/MrSago/MiSide-KappiMod/blob/f01a0763e18a6b8337cc4a831cadbed173c73c35/README.md#L103-L114); [project configurations](https://github.com/MrSago/MiSide-KappiMod/blob/f01a0763e18a6b8337cc4a831cadbed173c73c35/KappiMod.csproj#L13-L40)) | MiSide | Unity IL2CPP; BepInEx **or** MelonLoader build | [MIT LICENSE](https://github.com/MrSago/MiSide-KappiMod/blob/f01a0763e18a6b8337cc4a831cadbed173c73c35/LICENSE); separate release variants listed; no express reverse-engineering grant | Unresolved: corpus DLL cannot be assigned to the BepInEx variant without build identity | High project; low sample |
| `PotionCraft_EnableDev` ([exact-name repository search](https://github.com/search?q=PotionCraft_EnableDev&type=repositories)) | Potion Craft (inferred from corpus identifier) | Unresolved / unresolved | No authoritative first-party project, download page, or license located; no permission determination | Unresolved | Low |
| `TLD_PrepperCache` ([README](https://github.com/okclm/PrepperCache/blob/8e9b9a1a036b61fdd6b5ee89d8df75f4ee95a87e/README.md#L5-L7); [project file](https://github.com/okclm/PrepperCache/blob/8e9b9a1a036b61fdd6b5ee89d8df75f4ee95a87e/PrepperCache.csproj#L47-L82)) | The Long Dark | **MelonLoader** / IL2CPP | [MIT LICENSE.txt](https://github.com/okclm/PrepperCache/blob/8e9b9a1a036b61fdd6b5ee89d8df75f4ee95a87e/LICENSE.txt); README describes installing a DLL, but matching first-party binary listing was not verified; license has `[year] [fullname]` placeholders and no express reverse-engineering grant | **No for BepInEx-only coverage:** project explicitly targets MelonLoader | High project; low sample |
| `TLD_QualityOfLife` ([README](https://github.com/VonDoogles/TLD-QualityOfLife/blob/ec5f124cf4852b6e4da2a455f3ad3b5f7dbe60aa/README.md#L1-L5); [MelonMod source](https://github.com/VonDoogles/TLD-QualityOfLife/blob/ec5f124cf4852b6e4da2a455f3ad3b5f7dbe60aa/Source/QualityOfLifeMod.cs#L1-L6); [IL2CPP project references](https://github.com/VonDoogles/TLD-QualityOfLife/blob/ec5f124cf4852b6e4da2a455f3ad3b5f7dbe60aa/QualityOfLife.csproj#L6-L37)) | The Long Dark | **MelonLoader** / IL2CPP | [MIT LICENSE](https://github.com/VonDoogles/TLD-QualityOfLife/blob/ec5f124cf4852b6e4da2a455f3ad3b5f7dbe60aa/LICENSE); source public, matching DLL download unverified; no express reverse-engineering grant | **No for BepInEx-only coverage:** source derives from `MelonMod` | High project; low sample |
| `Tunic_Translation` ([README](https://github.com/holly-hacker/TunicTranslationMod/blob/47bdcdc42b2fb643fffeabf31795754ca4647a01/README.md#L1-L17); [plugin](https://github.com/holly-hacker/TunicTranslationMod/blob/47bdcdc42b2fb643fffeabf31795754ca4647a01/Plugin.cs#L1-L15)) | Tunic | BepInEx / IL2CPP | [BSD-2-Clause LICENSE](https://github.com/holly-hacker/TunicTranslationMod/blob/47bdcdc42b2fb643fffeabf31795754ca4647a01/LICENSE#L1-L13); README names `dev.variant9.Translation.dll`, but does not establish a public binary download; no express reverse-engineering grant | Unresolved: likely project match, but sample/build and license applicability unverified | High project; medium sample |

### Coverage disposition

The two TLD entries are confirmed MelonLoader projects and are excluded from a
BepInEx-only set. KappiMod is loader-ambiguous until its sampled variant is
identified. `PotionCraft_EnableDev` has no located authoritative project or
license. The strict eligibility count remains zero: for no entry was the
sampled DLL both matched to a licensed public artifact and shown to have a
license permitting its study/reverse engineering. No replacement meeting that
same bar was verified without downloading a binary; therefore the requested
30-item eligible corpus remains short, and no unverified substitute is
presented as eligible.
