#ifndef TEST_REPORT_H
#define TEST_REPORT_H

/*
 * Result reporting shared by the bare-metal self-tests.
 *
 * Every check prints "PASS <name>" or "FAIL <name>" and failures are
 * counted. test_report_finish() prints exactly one final line,
 * "SELFTEST PASS" or "SELFTEST FAIL <count>", which the host runner
 * (test/run_firmware_test.py) turns into the CTest result.
 *
 * The including file defines test_putc() for its output device.
 */
static void test_putc(char c);

static unsigned test_failures;

static void test_puts(const char *text)
{
	while (*text != '\0') {
		test_putc(*text);
		text++;
	}
}

static void test_put_decimal(unsigned value)
{
	char digits[10];
	unsigned count = 0U;

	do {
		digits[count] = (char)('0' + (value % 10U));
		value /= 10U;
		count++;
	} while (value != 0U);
	while (count > 0U) {
		count--;
		test_putc(digits[count]);
	}
}

static void test_check(const char *name, int passed)
{
	if (passed) {
		test_puts("PASS ");
	}
	else {
		test_puts("FAIL ");
		test_failures++;
	}
	test_puts(name);
	test_puts("\n");
}

static void test_report_finish(void)
{
	if (test_failures == 0U) {
		test_puts("SELFTEST PASS\n");
	}
	else {
		test_puts("SELFTEST FAIL ");
		test_put_decimal(test_failures);
		test_puts("\n");
	}
}

#endif /* TEST_REPORT_H */
