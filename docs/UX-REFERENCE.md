# UX patterns for the BepInEx Manager

This reference extracts concrete, neutral interaction patterns from first-party
sources. A pattern is not a claim that the whole product or its purpose should
be copied. Each Manager equivalent describes the current implementation and
gives an implementation-size estimate: **small** (one screen/control),
**medium** (multiple states or persistence), or **large** (cross-screen or
architectural work).

## LSPosed Manager

The official LSPosed README describes opening the manager from a notification;
the manager's own resources and source establish the UI labels and behaviors
below.

1. **Show health as named states, with context.** The Manager labels states
   including activated, partially activated, not installed, needs update,
   crashed, and mount failed, and exposes framework/device details. For a
   regular user, a specific state is more useful than an unexplained on/off
   indicator. **Our equivalent:** `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:267-283`
   displays root, module, and Zygisk status plus app version; it does not yet
   offer a similarly detailed diagnostic state per failure. **Cost: medium**
   to add actionable diagnostic detail without crowding the overview.
   [LSPosed Manager strings](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/res/values/strings.xml)

2. **Separate enabling an item from choosing its scope.** LSPosed exposes
   module enablement and a distinct target-app scope flow, including
   recommended and categorized targets. This makes the effect of a module
   easier to reason about and narrows accidental impact. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:480-506`
   has per-mod enable switches and options, but no comparable app-scope
   selection; mods are managed within a selected game's detail screen.
   **Cost: large**, because scope must be represented and enforced consistently
   by the loader and Manager. [LSPosed Manager strings](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/res/values/strings.xml);
   [LSPosed backup source](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/java/org/lsposed/manager/util/BackupUtils.java)

3. **Make large app lists filterable and sortable.** LSPosed names filters
   for system apps, games, modules, and denylisted apps, plus sorting by name,
   package, install time, or update time. People can narrow a long list using
   terms they recognize instead of scrolling blindly. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:300-322`
   supports name/package search and a games-only filter, but not sorting or
   additional categories. **Cost: small** for sorting and another filter
   control. [LSPosed Manager strings](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/res/values/strings.xml);
   [LSPosed app-list source](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/java/org/lsposed/manager/adapters/AppHelper.java)

4. **Expose recovery and diagnostics as deliberate actions.** LSPosed names
   module backup/restore and log save/clear/reload, and communicates when a
   reboot is required. These affordances help users recover or share useful
   information instead of guessing what to do after a change. **Our
   equivalent:** `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:143-147`
   links to per-game logs, and
   `manager/src/io/github/rianprei/bepinex/manager/LogViewerActivity.java:31-55`
   provides refresh/clear; no mod backup/restore action is present in those
   surfaces. **Cost: medium** for backup/restore with validation and clear
   feedback. [LSPosed Manager strings](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/res/values/strings.xml);
   [LSPosed backup source](https://github.com/LSPosed/LSPosed/blob/master/app/src/main/java/org/lsposed/manager/util/BackupUtils.java)

## GameGuardian

The cited evidence is GameGuardian's official scripting guide and API
reference. In particular, the scripting guide documents a workflow; it does
not prove the precise layout of the app's screens.

1. **Verify an operation manually before automating it.** The guide advises
   users to enumerate desired actions, perform them in the interface, and
   verify the behavior before mapping the actions to code. This is a general
   preview-first pattern that can catch misunderstandings before an automated
   change is applied. **Our equivalent:** `manager/src/io/github/rianprei/bepinex/manager/ModMakerActivity.java:132-155`
   offers scan, search, save/install, and export actions, but no distinct
   preview or dry-run stage. **Cost: medium** to add a review step and explain
   its limits. [Official simple-script guide](https://docs.gameguardian.net/simple_script.html)

2. **Treat recording as assisted capture, not finished automation.** The
   guide documents recording actions performed in the interface into a script
   and notes this is best suited to simple scripts or material for later
   editing. For ordinary users, capture can reduce repetitive entry while
   still setting expectations that review is needed. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/ModMakerActivity.java:132-155`
   has no interaction recorder. This is inspiration for a future guided
   authoring flow, not a recommendation to record arbitrary game actions.
   **Cost: large** for reliable recording and safe review. [Official simple-script guide](https://docs.gameguardian.net/simple_script.html)

3. **Keep authoring, running, and checking as separate stages.** The guide
   instructs users to save a script, run it, and check correctness as distinct
   actions. Separating these concepts makes it clearer what has merely been
   prepared versus what has taken effect. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/ModMakerActivity.java:632-635`
   reports a saved-and-activated mod in one result; there is no separate
   validation result here. **Cost: medium** to provide explicit validation
   feedback, provided a safe host-side check is available. [Official simple-script guide](https://docs.gameguardian.net/simple_script.html)

4. **Make the selected target inspectable.** The official API reference says
   scripts can retrieve selected-process metadata such as package, version,
   architecture, PID, and memory use, or receive nil if unavailable. This is
   API evidence, not proof of a particular visible screen. The neutral UX
   lesson is to show which target is in scope and its useful identifying
   details before an operation. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:270-282`
   reports install success/failure, while
   `manager/res/layout/activity_game_detail.xml:12-54` provides a game header
   with package and engine identity. **Cost: small** to keep target identity
   visible in confirmation and result messages. [Official `gg` API reference](https://docs.gameguardian.net/classgg.html)

## Lucky Patcher

**No UX patterns are included because none could be verified from accessible
first-party evidence.** The official site, [luckypatchers.com](https://luckypatchers.com/),
returned HTTP 403 when consulted. I did not bypass that restriction or use
third-party screenshots, mirrors, or write-ups as substitutes. The repository
and available evidence consulted did not establish neutral UI details that
could be cited confidently. This is an evidence limitation, not a claim that
the app lacks those patterns. IAP/license bypass and online-cheating behavior
are explicitly out of scope.

## MT Manager

MT Manager's official quick-start guide describes a two-pane file manager and
documents the interactions below.

1. **Use the other pane as the immediate transfer destination.** The guide
   says copy, move, archive extraction, and archive additions can transfer
   directly from the current pane to the other, without a separate paste
   action. For ordinary file tasks, this makes source and destination visible
   together and removes an intermediate step. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:121-125`
   opens the system document picker and
   `manager/src/io/github/rianprei/bepinex/manager/DownloadFileDialog.java:20-49`
   provides a Download fallback; there is no paired source/destination view.
   **Cost: large** and likely not justified for the current single-file
   installation workflow. [Official quick-start guide](https://mt.cc/guide/)

2. **Synchronize panes when the destination is in the current location.**
   MT Manager documents a Sync action that mirrors the current pane into the
   other, after which the user can navigate to a same-pane destination and
   transfer. The useful general pattern is preserving a consistent interaction
   model while offering an explicit route for an edge case. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/DownloadFileDialog.java:20-49`
   stages a chosen Download file for import; no pane synchronization exists.
   **Cost: large** for a two-pane file workflow. [Official quick-start guide](https://mt.cc/guide/)

3. **Pair multi-selection with understandable batch actions.** The guide
   documents swipe-to-select, contiguous-range selection, select-all,
   invert-selection, and select-by-type. This helps users act on many similar
   files without repeating the same gesture. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:458-523`
   renders each mod with individual controls; no multi-mod selection or batch
   action is present. **Cost: medium** for batch enable/disable with clear
   result reporting. [Official quick-start guide](https://mt.cc/guide/)

4. **Keep frequent destinations one gesture away with bookmarks.** The guide
   documents bookmarking commonly used paths/files and opening them from the
   bottom bar. A focused shortcut can save repeated navigation for recurring
   tasks. **Our equivalent:** `manager/src/io/github/rianprei/bepinex/manager/DownloadFileDialog.java:20-49`
   lists Download/Documents candidates, while
   `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:121-125`
   delegates selection to Android's document picker; neither implements
   saved locations. **Cost: medium** to persist shortcuts and handle missing
   paths safely. [Official quick-start guide](https://mt.cc/guide/)

5. **Reveal advanced search and path entry from familiar controls.** The
   guide documents long-pressing Sync for keyword filtering (including
   version-qualified regular-expression and negative matching) and
   long-pressing the parent-directory control for direct path navigation. The
   general pattern is progressive disclosure: simple use stays simple while
   advanced users have a faster path. **Our equivalent:**
   `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:300-322`
   filters apps by typed name/package and game-only state; it has no advanced
   filter syntax or path navigation. **Cost: small** for richer search
   controls; path navigation is not relevant to the current app-list screen.
   [Official quick-start guide](https://mt.cc/guide/)

## Top five to adapt first

These are adaptations for this Manager, not a ranking of the reference apps.

1. **Add sort choices to the existing game search/filter.** The filter is
   already present, so sorting by app name/package can improve navigation at
   low cost. **Cost: small.** Current surface:
   `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:300-322`.
2. **Make status failures more actionable.** Keep the existing concise status
   overview, but let a user distinguish a missing component from a disabled
   one and see the next safe action. **Cost: medium.** Current surface:
   `manager/src/io/github/rianprei/bepinex/manager/MainActivity.java:267-283`.
3. **Add validated mod backup/restore.** Export/import should identify the
   target game, validate content before applying, and report partial or failed
   restoration explicitly. **Cost: medium.** Existing mod controls:
   `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:480-523`.
4. **Add batch enable/disable for a game's mod list.** Select multiple mods,
   show the count and intended action, then summarize outcomes. This adapts
   MT Manager's batch-selection pattern without copying its file-pane UI.
   **Cost: medium.** Current per-mod controls:
   `manager/src/io/github/rianprei/bepinex/manager/GameDetailActivity.java:480-493`.
5. **Separate mod preparation from validation feedback.** Where a meaningful
   safe check exists, show what will be installed and its target before
   applying it; distinguish successful installation from a validated runtime
   result. Do not imply a mod is safe or compatible merely because it was
   packaged. **Cost: medium.** Current authoring flow:
   `manager/src/io/github/rianprei/bepinex/manager/ModMakerActivity.java:132-155`.
