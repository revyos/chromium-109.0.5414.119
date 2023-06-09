# Interactive Testing API: "Kombucha"

**Kombucha** is a group of powerful test mix-ins that let you easily and
concisely write interactive tests.

The current API version is 1.51. All future 1.x versions are guaranteed to be
backwards-compatible with existing tests.

[TOC]

## Getting Started

There are two ways to write a Kombucha-based interaction test:
1. Alias or inherit from one of our pre-configured test fixtures (preferred):
    - [InteractiveTest](/ui/base/interaction/interactive_test.h)
    - [InteractiveViewsTest](/ui/views/interaction/interactive_views_test.h)
    - [InteractiveBrowserTest](/chrome/test/interaction/interactive_browser_test.h)
2. Have your test fixture inherit the appropriate Kombucha API class:
    - [InteractiveTestApi](/ui/base/interaction/interactive_test.h)
    - [InteractiveViewsTestApi](/ui/views/interaction/interactive_views_test.h)
    - [InteractiveBrowserTestApi](/chrome/test/interaction/interactive_browser_test.h)

If you go the latter route, please see
[Custom Test Fixtures](#custom-test-fixtures) below.

## Using the Kombucha API

***Note:** Throughout this section, unless otherwise specified, all methods are
present in `InteractiveTestApi`. If a method is introduced in
`InteractiveViewsTestApi`, it will have **[Views]** next to it; if it's
introduced in `InteractiveBrowserTestApi`, it will have **[Browser]** next to it
instead.*

### Test Sequences

The primary entry point for any test is `RunTestSequence()` [Views] or
`RunTestSequenceInContext()`. (For more information on `ElementContext`, see the
[Interaction Library Documentation](/ui/base/interaction/README.md).)

`RunTestSequence()` is designed to accept any number of steps. You will use the
provided palette of test verbs and checks that the API provides, or [create your
own verbs](#custom-verbs) through generator methods. The steps are executed in
order until either the final step completes, which is considered success, or a
step fails (or the test times out), which are considered failures.

Example:

```cpp
class MyDialogTest : public InteractiveBrowserTest { ... };

IN_PROC_BROWSER_TEST_F(MyDialogTest, Apply) {
  RunTestSequence(
      // Open my dialog.
      PressButton(kShowMyDialogButtonId),
      WaitForShow(kMyDialogId),
      // Validate the caption of the Apply button.
      CheckViewProperty(kApplyButtonId, &LabelButton::GetText, u"Apply"),
      // Press the button and verify that the state is applied.
      PressButton(kApplyButtonId),
      // This is a custom verb created for this test suite.
      VerifyChangesApplied());
}
```

### Test Verbs

Kombucha *test verbs* are methods that you can call in your test which generate
test steps. Most verbs take an `ElementSpecifier` - either an ID or a name -
describing which UI element the verb should be applied to, but not all do. Some
verbs, like `Check()` and `Do()` don't care about specific elements.

Verbs fall into a number of different categories:
- **Do** performs an action you specify.
- **Check** verbs ensure that some condition is true; if it is not, the test
  fails. Some *Check* verbs use `Matcher`s, some use callbacks, etc. Examples
  include:
    - `Check()`
    - `CheckResult()`
    - `CheckElement()`
    - `CheckView()` [Views]
    - `CheckViewProperty()` [Views]
    - `Screenshot` [Browser] - compares the target against Skia Gold in pixel
      tests
- **WaitFor** verbs ensure that the given UI event happens before proceeding.
  Examples:
    - `WaitForShow()`
    - `WaitForHide()`
    - `WaitForActivated()`
    - `WaitForEvent()`
    - `WaitForWebContentsReady()` [Browser]
    - `WaitForWebContentsNavigation()` [Browser]
    - `WaitForStateChange()` [Browser]
- **After** verbs allow you to take some action (specified as a callback) when a
  given event takes place or condition becomes true. The callback can be a full
  `InteractionSequence::StepStartCallback` or it can omit any number of leading
  arguments; try to be as concise as possible. Examples:
    - `AfterShow()`
    - `AfterHide()`
    - `AfterActivated()`
    - `AfterEvent()`
- **WithElement** gets the specified element and performs the specified action.
  Unlike the above verbs, it will not wait; the element must exist when the step
  triggers or the test will fail.
- **EnsureNotPresent** is the opposite of `WithElement`; if the element exists
  when the step is triggered, the test fails.
- **Action** verbs simulate input to specific UI elements. You may specify the
  type of input you want to simulate (keyboard, mouse, etc.) but you don't have
  to. Examples:
    - `PressButton()`
    - `SelectMenuItem()`
    - `SelectTab()`
    - `DoDefaultAction()`
    - `NavigateWebContents()` [Browser]
- **Mouse** verbs simulate mouse input to the entire application, and are
  therefore only reliable in test fixtures that run as exclusive processes (e.g.
  interactive_browser_tests). Examples include:
    - `MoveMouseTo()` [Views]
    - `DragMouseTo()` [Views]
    - `ClickMouse()` [Views]
    - `ReleaseMouseButton()` [Views]
- **Name** verbs assign a string name to some UI element which may not be known
  ahead of time, so that it can be referenced later in the test. Examples
  include:
    - `NameView()` [Views]
    - `NameChildView()` [Views]
    - `NameDescendantView()` [Views]
    - `NameViewRelative()` [Views]

Example with mouse input:
```cpp
// Navigate a page, click Back, and verify that the page navigates back
// correctly.
RunTestSequence(
    NavigateWebContents(kActiveTabId, kTargetUrl),
    MoveMouseTo(kBackButtonId),
    ClickMouse(),
    WaitForWebContentsNavigation(kPreviousUrl));
```

Example with a named element:
```cpp
RunTestSequence(
    // Identify the first child view of the button container.
    NameChildView(kDialogButtons, kFirstButton, 0),
    // Verify that this is the OK button.
    CheckViewProperty(kFirstButton,
                      &LabelButton::GetText,
                      l10n_util::GetStringUTF16(IDS_OK)),
    // Press the button.
    PressButton(kFirstButton));
```

### Modifiers

A modifier wraps around a step and changes its behavior. Currently there is only one modifier:
- **InAnyContext** allows the modified verb to find an element by its identifier
  outside the test's default `ElementContext`. It has no effect on
  `EnsureNotPresent()`, and is not compatible with named elements or with some
  specific verbs. Use sparingly.

Example:
```cpp
RunTestSequence(
    // This button might be in a different window!
    InAnyContext(PressButton(kMyButton)),
    InAnyContext(CheckView(kMyButton, ensure_pressed)));
```

### Instrumentation

A feature of `InteractiveBrowserTestApi` that it borrows from
[WebContentsInteractoinTestUtil](/chrome/test/interaction/webcontents_interaction_test_util.h)
is the ability to *instrument* a `WebContents`. This does the following:
- Assigns the entire `WebContents` an `ElementIdentifier`.
- Enables a number of page navigation verbs, such as `NavigateWebContents()`
  and `WaitForWebContentsReady()`.
- Allows the execution of arbitrary JS in the WebContents.
- Allows waiting for a specific condition in the DOM of the `WebContents` via
  `WaitForStateChange()`.

You may call **Instrument** methods before or during a test sequence.
- `InstrumentTab()` instruments an existing tab.
- `InstrumentNextTab()` instruments the next tab to be added to or opened in the
  specified browser.
- `InstrumentNonTabWebContents()` instruments a piece of primary or secondary UI
  that uses a `WebView` and is not a tab (e.g. the tablet tabstrip or Tab Search
  dialog).

### Automatic Conversion

The following convenience methods are provided to convert a `TrackedElement*` to
a more specific object, primarily used in callbacks supplied to `WithElement()`
or one of the **After** verbs:
- `AsView<T>` - converts the element to a view of the specific type; fails if it
  is not
- `AsInstrumentedWebContents()` - converts the element to an instrumented
  `WebContents`; fails if it is not

Example:
```cpp
  WithElement(kComboBoxId, base::BindOnce([](ui::TrackedElement* el){
    // Note to self: we should probably just have a verb for this.
    AsView<ComboBox>(el)->SelectItem(1);
  })),
```

### Custom Verbs

Sometimes you will have some common step or check (or set of steps and checks)
that you want to duplicate across a number of different test cases in your test
fixture. You can create a custom verb, which is just a method that returns a
`StepBuilder` or `MultiStep`. This method can combine existing verbs with steps
you create yourself, in any combination. To combine multiple steps, use the
`Steps()` method.

Here's an example of a very common custom verb pattern:

```cpp
// My test fixture class with a custom verb.
class MyHistoryTest : public InteractiveBrowserTest {

  // This custom verb will be used across multiple test cases.
  auto OpenHistoryPageInNewTab() {
    return Steps(
        Do(base::BindLambdaForTesting(
            [this](){ InstrumentNextTab(browser(), kHistoryPageTab); }))
        PressButton(kNewTabButton),
        PressButton(kAppMenuButton),
        SelectMenuItem(kHistoryMenuItem),
        SelectMenuItem(kOpenHistoryPageMenuItem),
        WaitForWebContentsNavigation(kHistoryPageTabId,
                                     chrome::kHistoryPageUrl));
  }
};

// An example test case.
IN_PROC_BROWSER_TEST_F(MyHistoryTest, NavigateTwoPagesAndCheckHistory) {
  InstrumentTab(browser(), kPrimaryTabId);
  RunTestSequence(
    WaitForWebContentsReady(kPrimaryTabId),
    NavigateWebContents(kPrimaryTabId, kUrl1),
    NavigateWebContents(kPrimaryTabId, kUrl2),
    // The custom verb sits happily in the action sequence.
    OpenHistoryPageInNewTab(),
    // We'll hand-wave the implementation of this method for now.
    WaitForStateChange(kHistoryPageTabId,
                       HistoryEntriesPopulated(kUrl1, kUrl2)));
}
```

### Custom Callbacks and Checks

Another common pattern is having a check that you want to perform over and over;
for example, checking that a histogram entry was added. This can absolutely be
done through a custom verb, however, perhaps you instead want to use it in an
`AfterShow()` step. In this case you can create a function that binds and
returns the appropriate callback.

```cpp
class MyDialogTest : public InteractiveBrowserTest {
  auto ExpectHistogramCount(const char* histogram_name, size_t expected_count) {
    return base::BindLambdaForTesting([histogram_name, expected_count, this](){
      EXPECT_EQ(expected_count, GetHistogramCount(histogram_name));
    });
  }
};

IN_PROC_BROWSER_TEST_F(MyDialogTest, ShowIncrementsHistogram) {
  RunTestSequence(
    PressButton(kShowMyDialogButtonId),
    AfterShow(kMyDialogContentsId,
              ExpectHistogramCount(kDialogShownHistogramName, 1)));
}
```

This could have also been implemented with a `WaitForShow()` and a custom verb
with a `Check()` or `CheckResult()`. Whether you use a custom callback or a
custom verb is up to you; do whatever makes your test easiest to read!

## Custom Test Fixtures

Most Kombucha tests will derive directly from either `InteractiveViewsTest` or
`InteractiveBrowserTest`.

If your test needs to derive from a different/custom test fixture class but you
would still like access to the Kombucha API, you can have your fixture inherit
directly from one of the *TestApi classes above. (This happens most commonly
when you are adding Kombucha tests to a large library of existing feature
tests.)

You will then need to insert the following calls:
- In `SetUp()` (or `SetUpOnMainThread()` for browser tests), you will need to
  call `private_test_impl().DoTestSetUp()`.
- In `TearDown()` (or `TearDownOnMainThread()` for browser tests), you will
  need to call `private_test_impl().DoTestTearDown()`.

For tests deriving from `InteractiveViewsTestApi` or any of its subclasses, you
will also need to call `SetContextWidget()` sometime before you call
`RunTestSequence()`.

See the implementations of any of the convenience test fixtures listed in
[Getting Started](#getting-started) for examples.

Failure to do the above may cause your test to malfunction, or some test verbs
not to work.

Example:

```cpp
class MyTestFixture
    : public MyCustomBrowserTest,  // descends from InProcessBrowserTest
      public InteractiveBrowserTestApi {
 public:
  void SetUpOnMainThread() override {
    MyCustomTestBase::SetUpOnMainThread();
    private_test_impl().DoTestSetUp();
    // It's safest to do this here; you can still call
    // RunTestSequenceInContext() if you need a different context (e.g. an
    // incognito browser window).
    SetContextWidget(
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget());
  }

  void TearDownOnMainThread() override {
    // Optional, but polite:
    SetContextWidget(nullptr);
    private_test_impl().DoTestTearDown();
    MyCustomTestBase::TearDownOnMainThread();
  }
};
```

## Helper Classes

Kombucha helper classes are older, lower-level APIs that have been repurposed
to support interactive testing:
- `InteractionTestUtil`, `InteractionTestUtilView`,
  `InteractionTestUtilBrowser` - provide common UI functionality like pressing
  buttons, selecting menu items, and taking screenshots.
- `InteractionTestUtilMouse` - provides a way to inject mouse input, including
  clicking and dragging, into interactive tests.
- `WebContentsInteractionTestUtil` - provides a way to gain control of a
  WebContents, inject code, trigger and wait for navigation, and check and wait
  for changes in the DOM.

You should only rarely have to use these classes directly; if you do, it's
likely that Kombucha is missing some common verb that would cover your use case.
Please reach out to us!
