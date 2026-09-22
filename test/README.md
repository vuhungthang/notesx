# GTEST testing framework

## How to add a new test

Add a new `.cpp` file somewhere within `test/unit_tests`.
This test will be automatically discovered by gtest (see `gtest_discover_tests` in `CMakeLists.txt`).
However, **you still need to call** `touch test/CMakeLists.txt` as otherwise the `GLOB` recursive lookup for test files will not be triggered!!!

A basic example for a test file would be 

```cpp
#include <gtest/gtest.h>

// Demonstrate some basic assertions.
TEST(HelloTest, BasicAssertions) {
  // Expect two strings not to be equal.
  EXPECT_STRNE("hello", "world");
  // Expect equality.
  EXPECT_EQ(7 * 6, 42);
}
```
as taken from the official [docs](http://google.github.io/googletest/quickstart-cmake.html).

As all `test/unit_tests` are built with a dependency on `xournalpp-core` you can include any file from `src` as you would in the main code.

## How to migrate existing CPPUnit Test code

It's mostly:

* remove all code, apart from test functions, helper functions and includes (e.g class structure)
* replace `void <name of testcase>() {`  
  with  `TEST(<NAME of your TestSuite>, <name of testcase>) {`
* replace `CPPUNIT_ASSERT_EQUAL`  
  with `EXPECT_EQ`

## How to add a new test program

First why: This would make sense in case you have a very different testing usecase than with unit-tests and you might want to collect code coverage separately. Otherwise, adding to unit-tests is way simpler and preferred as  `gtest_discover_tests` does all the book keeping for you then.

* create a new folder for your test program
* add the definition of your test program to `test/CMakeLists.txt`
* add the test program to all jobs in `azure-pipelines/continuous-integration.yml`

For further pointers see the official [Quickstart Cmake Guide](http://google.github.io/googletest/quickstart-cmake.html).

## GTK integration tests

`test/gtk_tests` holds the tests that need a real GTK3 widget tree: they build widgets, put them in a window and drive them the way a user would. They are built into their own executable (`test-gtk-integration`, from `test/gtk_tests/*.cpp`) because they need a display and a `GtkApplication`, which the plain unit tests do not have.

```sh
cmake --build build --target test-gtk-integration
xvfb-run -a ./test/test-gtk-integration
```

CI runs the same executable as `CI=true xvfb-run -a ./test/test-gtk-integration`. As for the unit tests, a recursive `GLOB` picks the files up, so `touch test/CMakeLists.txt` after adding one.

### What these tests can and cannot see

They assert structure, wiring and state: which widgets exist, what they are called for accessibility, which action a control is bound to, and whether activating a control changes that action's state. They cannot see anything the renderer draws, so how a control *looks* stays a manual check.

They also cannot see a popover close. GTK pops a popover down by hiding its own `GdkWindow`, and the harness never maps that window through a window manager, so the popover still reports itself as visible afterwards. A test can assert what the popdown depends on - that the popover is anchored to the control that opened it and that it is modal - and that GTK itself consumes `Escape` on it while leaving a plain key alone, but not the popdown that follows.

| To verify by hand | How |
| --- | --- |
| The tool properties popover opens on the tool button and shows only the fields of that tool | Open the pen, the eraser and the highlighter in turn; compare the panels |
| The stroke preview at the head of a size row has the thickness of its size | Compare the five width rows of the pen |
| `Escape` closes the popover and the focus returns to the control that opened it | Open a tool's properties, press `Escape`, then press `Space` or an arrow key |
| A click outside the popover closes it | Open a tool's properties, then click in the document |
| The favourite strip follows a change of the preset list | Add and remove a favourite, then reopen the toolbar |
| The active-tool summary follows the tool | Switch tools and watch the item |
| The Focus layout keeps one row | Resize the window with the Focus layout selected |

### Known limitation: a GTK3 radio group cannot be emptied

`gtk_toggle_button_set_active(..., FALSE)` on the member of a radio group that is currently active is a no-op in GTK3, so a radio group always shows one of its members as chosen. The tool properties panel therefore shows the first drawing type as chosen while the pen is on freehand, which has no drawing type and hence no row of its own. The state is correct; only the highlight is misleading. Making freehand a row of its own, or dropping the radio buttons for something that can be cleared, would fix it.

## Problems running `make test`

If CMake is generating UNIX Makefiles and `make test` fails with  the error `Unable to find executable: test-units_NOT_BUILT`, make sure that:
 1. `make test-units` has been run (to compile the test executable)
 2. The project was configured with `-DENABLE_GTEST=ON` (e.g. by configuring with `cmake .. -DENABLE_GTEST=ON`)

 The fact that 1. is not handled automatically seems to be a known limitation of CMake (see [issue](https://gitlab.kitware.com/cmake/cmake/-/issues/8774) and [stackoverflow](https://stackoverflow.com/questions/733475/cmake-ctest-make-test-doesnt-build-tests)).
 One possible workaround is to "build & run" the `test-units`executable directly.
 This way the tests are always built before they are executed.

## Problems Building on MacOSX

If you get a linking error of the sort,

```
[317/317] Linking CXX executable test/test-units
FAILED: test/test-units test/test-units[1]_tests.cmake 

...

dyld: Library not loaded: @rpath/libpoppler-glib.8.dylib
  Referenced from: /Users/runner/work/1/s/build/test/test-units
  Reason: image not found
```

then you have some fun `@rpath` problem.
The `test/CMakeLists.txt` already contains a solution for this, so that is a good start for debugging if something related to MacOSX build failures would come up.

Apart from that resources I found useful:
 * [Understanding RPATH (with CMake)](https://dev.my-gate.net/2021/08/04/understanding-rpath-with-cmake/)
 * [RPATH handling](https://gitlab.kitware.com/cmake/community/-/wikis/doc/cmake/RPATH-handling)

## Further Reference

* [GoogleTest User’s Guide](http://google.github.io/googletest/)
* [CPPUnit project page](http://cppunit.sourceforge.net/doc/cvs/group___assertions.html) (for migration)
