#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <internal/_tls.h>

static struct thread_storage test_tls;
static errno_t test_errno;
static int failures;

#define EXPECT_TRUE(expr) do { if (!(expr)) failures++; } while (0)
#define EXPECT_NULL(expr) EXPECT_TRUE((expr) == NULL)
#define EXPECT_NOT_NULL(expr) EXPECT_TRUE((expr) != NULL)
#define EXPECT_PTR_EQUAL(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_INT_EQUAL(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_STRING_EQUAL(a, b) EXPECT_TRUE(strcmp((a), (b)) == 0)

errno_t *__errno(void)
{
    return &test_errno;
}

struct thread_storage *__tls_current(void)
{
    return &test_tls;
}

static void setup_locale(void)
{
    memset(&test_tls, 0, sizeof(test_tls));
    test_errno = 0;
    setlocale(LC_ALL, "C");
}

static void test_c_locale_basics(void)
{
    struct lconv *conversion;

    EXPECT_STRING_EQUAL(setlocale(LC_ALL, NULL), "C");
    EXPECT_STRING_EQUAL(setlocale(LC_ALL, "POSIX"), "C");
    EXPECT_STRING_EQUAL(__locale_encoding_l(NULL), "ASCII");
    EXPECT_INT_EQUAL(__locale_mb_cur_max(), 1);

    conversion = localeconv();
    EXPECT_NOT_NULL(conversion);
    EXPECT_STRING_EQUAL(conversion->decimal_point, ".");
}

static void test_c_utf8_locale_is_supported(void)
{
    EXPECT_STRING_EQUAL(setlocale(LC_CTYPE, "C.UTF-8"), "C.UTF-8");
    EXPECT_STRING_EQUAL(__locale_encoding_l(NULL), "UTF-8");
    EXPECT_INT_EQUAL(__locale_mb_cur_max(), 6);
}

static void test_non_tier_b_locale_is_rejected(void)
{
    EXPECT_NULL(setlocale(LC_CTYPE, "en_US.UTF-8"));
    EXPECT_INT_EQUAL(errno, ENOENT);
    EXPECT_STRING_EQUAL(setlocale(LC_CTYPE, NULL), "C");
}

static void test_locale_object_lifecycle(void)
{
    locale_t locale;
    locale_t duplicate;

    locale = newlocale(LC_ALL_MASK, "C.UTF-8", NULL);
    EXPECT_NOT_NULL(locale);
    EXPECT_STRING_EQUAL(__locale_encoding_l(locale), "UTF-8");

    duplicate = duplocale(locale);
    EXPECT_NOT_NULL(duplicate);
    EXPECT_STRING_EQUAL(__locale_encoding_l(duplicate), "UTF-8");

    freelocale(locale);
    freelocale(duplicate);
}

static void test_uselocale_global_mode(void)
{
    locale_t locale;

    EXPECT_PTR_EQUAL(uselocale(NULL), LC_GLOBAL_LOCALE);

    locale = newlocale(LC_ALL_MASK, "C.UTF-8", NULL);
    EXPECT_NOT_NULL(locale);
    EXPECT_PTR_EQUAL(uselocale(locale), LC_GLOBAL_LOCALE);
    EXPECT_PTR_EQUAL(uselocale(NULL), locale);
    EXPECT_STRING_EQUAL(__locale_encoding_l(NULL), "UTF-8");
    EXPECT_PTR_EQUAL(uselocale(LC_GLOBAL_LOCALE), locale);
    EXPECT_PTR_EQUAL(uselocale(NULL), LC_GLOBAL_LOCALE);

    freelocale(locale);
}

int main(void)
{
    setup_locale();
    test_c_locale_basics();
    setup_locale();
    test_c_utf8_locale_is_supported();
    setup_locale();
    test_non_tier_b_locale_is_rejected();
    setup_locale();
    test_locale_object_lifecycle();
    setup_locale();
    test_uselocale_global_mode();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}