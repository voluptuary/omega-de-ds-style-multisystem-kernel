#include <stdio.h>
#include <stdlib.h>

#include "launcher_navigation.h"

static void expect(unsigned int actual, unsigned int expected, const char *message)
{
	if(actual != expected)
	{
		fprintf(stderr, "FAIL: %s (got %u, expected %u)\n", message, actual, expected);
		exit(1);
	}
}

static void test_single_step(void)
{
	expect(LauncherNavigationStepIndex(4, 5, 1, 0), 4,
		"wrap defaults off at the last item");
	expect(LauncherNavigationStepIndex(4, 5, 1, 1), 0,
		"forward wraps from last to first");
	expect(LauncherNavigationStepIndex(0, 5, -1, 0), 0,
		"wrap defaults off at the first item");
	expect(LauncherNavigationStepIndex(0, 5, -1, 1), 4,
		"reverse wraps from first to last");
	expect(LauncherNavigationStepIndex(2, 5, 1, 1), 3,
		"forward step inside the list");
	expect(LauncherNavigationStepIndex(2, 5, -1, 1), 1,
		"reverse step inside the list");
	expect(LauncherNavigationStepIndex(0, 1, 1, 1), 0,
		"single-item list does not move");
	expect(LauncherNavigationStepIndex(0, 0, 1, 1), 0,
		"empty list does not move");
}

static void test_page_jump(void)
{
	expect(LauncherNavigationJumpIndex(4, 15, 1, 10, 0), 14,
		"forward page jump lands on the last item");
	expect(LauncherNavigationJumpIndex(5, 15, 1, 10, 1), 0,
		"forward page jump wraps when it crosses the last item");
	expect(LauncherNavigationJumpIndex(10, 15, 1, 10, 1), 0,
		"forward page jump wraps from the final page");
	expect(LauncherNavigationJumpIndex(5, 15, -1, 10, 0), 0,
		"reverse page jump clamps when wrap is off");
	expect(LauncherNavigationJumpIndex(5, 15, -1, 10, 1), 14,
		"reverse page jump wraps when it crosses the first item");
	expect(LauncherNavigationJumpIndex(10, 15, -1, 10, 1), 0,
		"reverse page jump can land exactly on the first item");
	expect(LauncherNavigationJumpIndex(0, 0, 1, 10, 1), 0,
		"empty-list page jump does not move");
}

int main(void)
{
	test_single_step();
	test_page_jump();
	puts("Launcher navigation scenarios passed.");
	return 0;
}
