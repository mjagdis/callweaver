#include <stdio.h>

#include <callweaver/callweaver_hash.h>


struct {
	const char *text;
	unsigned int hash;
} words[] = {
	{ "ANSWERED", 0 },
	{ "CALLERID", 0 },
	{ "CALLERIDNUM", 0 },
	{ "CALLERIDNAME", 0 },
	{ "CALLERANI", 0 },
	{ "CALLINGPRES", 0 },
	{ "CALLINGANI2", 0 },
	{ "CALLINGTON", 0 },
	{ "CALLINGTNS", 0 },
	{ "DIALSTATUS", 0 },
	{ "DNID", 0 },
	{ "HINT", 0 },
	{ "HINTNAME", 0 },
	{ "EXTEN", 0 },
	{ "RDNIS", 0 },
	{ "CONTEXT", 0 },
	{ "PRIORITY", 0 },
	{ "CHANNEL", 0 },
	{ "UNIQUEID", 0 },
	{ "HANGUPCAUSE", 0 },
	{ "NEWDESTNUM", 0 },
	{ "ACCOUNTCODE", 0 },
	{ "LANGUAGE", 0 },
	{ "SYSTEMNAME", 0 },

	// ---------------------------------------------------------------------------
	// Values for the DIALSTATUS channel variable
	// ---------------------------------------------------------------------------

	{ "ANSWER", 0 },
	{ "BARRED", 0 },
	{ "BUSY", 0 },
	{ "CANCEL", 0 },
	{ "CHANUNAVAIL", 0 },
	{ "CONGESTION", 0 },
	{ "NOANSWER", 0 },
	{ "NUMBERCHANGED", 0 },
	{ "REJECTED", 0 },
	{ "UNALLOCATED", 0 },


	// ---------------------------------------------------------------------------
	// Built-in global variable names used in pbx.c
	// ---------------------------------------------------------------------------

	{ "EPOCH", 0 },
	{ "DATETIME", 0 },
	{ "TIMESTAMP", 0 },


	// ---------------------------------------------------------------------------
	// Modifiers used in pbx.c
	// ---------------------------------------------------------------------------

	{ "SKIP", 0 },
	{ "BYEXTENSION", 0 },


	// ---------------------------------------------------------------------------
	// Dialplan functions
	// ---------------------------------------------------------------------------

	{ "Dial", 0 },
	{ "Directory", 0 },
	{ "Muxmon", 0 },
	{ "Monitor", 0 },
	{ "NConference", 0 },
	{ "OGI", 0 },
	{ "Proc", 0 },
	{ "Record", 0 },
};


int main(int argc, char *argv[])
{
	int i, j, ret;
	const char *p;

	puts("#ifndef _CALLWEAVER_CALLWEAVER_KEYWORDS_H");
	puts("#define _CALLWEAVER_CALLWEAVER_KEYWORDS_H");
	putchar('\n');

	ret = 0;
	for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
		for (p = words[i].text; *p; p++)
			words[i].hash = cw_hash_add(words[i].hash, *p);
		printf("#define CW_KEYWORD_%s\t0x%08x\n", words[i].text, words[i].hash);
		for (j = 0; j < i; j++) {
			if (words[j].hash == words[i].hash) {
				fprintf(stderr, "Error: hash collision: hash=0x%08x from \"%s\" and \"%s\"\n",
					words[i].hash, words[j].text, words[i].text);
				ret = 1;
			}
		}
	}

	putchar('\n');
	puts("#endif");

	return ret;
}
