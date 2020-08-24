#include "test_iotm.h"
#include "iotm_data_types.h"

void test_data_type_get_uint8(void)
{
    int err = -1;
    uint8_t check = 0x00;
	char *assign = "0xab";
	err = convert_to_type(assign, UINT8, &check);
	TEST_ASSERT_EQUAL_INT(0, err);
	TEST_ASSERT_EQUAL_UINT8(0xab, check);
}

void test_data_type_get_uint16(void)
{
    int err = -1;
    uint16_t check = 0x0000;
	char *assign = "0xab0c";
	err = convert_to_type(assign, UINT16, &check);
	TEST_ASSERT_EQUAL_INT(0, err);
	TEST_ASSERT_EQUAL_UINT8(0xab0c, check);
}

void test_data_type_uint8_to_char(void)
{
    int err = -1;
	uint8_t assign = 0xde;
	char check[100];
	err = convert_from_type(&assign, UINT8, check);
	TEST_ASSERT_EQUAL_INT(0, err);
	TEST_ASSERT_EQUAL_STRING("DE", check);
}

void test_data_type_get_string(void)
{
    int err = -1;
    char check[1024];
	char *assign = "checking";
	err = convert_to_type(assign, STRING, &check);
	TEST_ASSERT_EQUAL_INT(0, err);
	TEST_ASSERT_EQUAL_STRING("checking", check);
}

void test_data_type_string_to_char(void)
{
    int err = -1;
	char *start = "start";
	char check[100];
	err = convert_from_type(start, STRING, check);
	TEST_ASSERT_EQUAL_INT(0, err);
	TEST_ASSERT_EQUAL_STRING("start", check);
}

void test_data_type_int_to_char(void)
{
    int err = -1;
    int start = 42;
    char check[100];
    err = convert_from_type(&start, INT, check);
	TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_STRING("42", check);
}

void test_data_type_char_to_int(void)
{
    int err = -1;
    char *start = "42";
    int check = -1;
    err = convert_to_type(start, INT, &check);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(42, check);
}

void test_data_type_time_to_char(void)
{
    int err = -1;
    time_t start = time(NULL);
    time_t end = start;

    time_t check_end = -1;
    char check[100];
    strcpy(check, "tmp");
    err = convert_from_type(&start, LONG, check);
    TEST_ASSERT_EQUAL_INT(0, err);
    err = convert_to_type(check, LONG, &check_end);
    TEST_ASSERT_EQUAL_INT(end, check_end);
}

void test_data_type_hex_to_char(void)
{
    int err = -1;
    unsigned char tmp[2] = { 0xab, 0xfe };
    iot_barray_t input =
    {
       .data = tmp,
       .data_length = 2,
    };

    char check[1024];

    unsigned char out_array[2];
    memset(out_array, 0, sizeof(*out_array));
    iot_barray_t check_end = 
    {
        .data = out_array,
        .data_length = 0,
    };

    err = convert_from_type(&input, HEX_ARRAY, check);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, err, "Converted from a byte array to a string");

    err = convert_to_type(check, HEX_ARRAY, &check_end);

    TEST_ASSERT_EQUAL_UINT_ARRAY_MESSAGE(tmp, check_end.data, 2, "Output bytee array same as input byte array");

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, check_end.data_length, "Converted the correct number of bytes");
}

void test_data_type_suite()
{
    RUN_TEST(test_data_type_get_uint8);
    RUN_TEST(test_data_type_get_uint16);
	RUN_TEST(test_data_type_uint8_to_char);
	RUN_TEST(test_data_type_get_string);
	RUN_TEST(test_data_type_string_to_char);
    RUN_TEST(test_data_type_int_to_char);
    RUN_TEST(test_data_type_char_to_int);
    RUN_TEST(test_data_type_time_to_char);
    RUN_TEST(test_data_type_hex_to_char);
}
