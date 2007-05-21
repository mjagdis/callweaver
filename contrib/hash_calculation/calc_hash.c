#include <stdio.h>
#include <string.h>
#define MAX_STR_LEN 80

int main (int argc, const char * argv[]) {
	unsigned int i, len, hash = 0;
	char ch, str[MAX_STR_LEN + 1];

	if (argc <= 1) {
		printf("Usage: calc_hash string\nExample: ./calc_hash ACCOUNTCODE will return 0x47D129A\n");
		return 1;
	} else {
		len = strlen(argv[1]);
		if (len > MAX_STR_LEN) {
			len = MAX_STR_LEN;
		}
		strncpy(str, argv[1], len);

		for (i = 0; i < len; i++) {
			ch = str[i];
			hash = ch + (hash << 6) + (hash << 16) - hash;
		}
		hash = (hash & 0x7FFFFFFF);

		printf("0x%8X\n", hash);

		return 0;
	}
}
