#include <stdio.h>

#include <callweaver/callweaver_hash.h>


struct {
	const char *text;
	unsigned int hash;
} words[] = {
	{ "ANSWERED", 0 },
	{ "ALERT_INFO", 0 },
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
	{ "VXML_URL", 0 },
	{ "SIP_URI_OPTIONS", 0 },
	{ "T38CALL", 0 },
	{ "OSPTOKEN", 0 },
	{ "OSPHANDLE", 0 },
	{ "OSPPEER", 0 },
	{ "EXITCONTEXT", 0 },
	{ "LIMIT_PLAYAUDIO_CALLER", 0 },
	{ "LIMIT_PLAYAUDIO_CALLEE", 0 },
	{ "LIMIT_WARNING_FILE", 0 },
	{ "LIMIT_TIMEOUT_FILE", 0 },
	{ "LIMIT_CONNECT_FILE", 0 },
	{ "OUTBOUND_GROUP", 0 },
	{ "DIALEDPEERNUMBER", 0 },
	{ "MISDN_URATE", 0 },
	{ "BLINDTRANSFER", 0 },
	{ "VM_CATEGORY", 0 },
	{ "ALREADY_WAITED", 0 },
	{ "WAIT_STOPKEY", 0 },
	{ "PRI_CAUSE", 0 },
	{ "CALLERTON", 0 },
	{ "CALLINGSUBADDRESS", 0 },
	{ "CALLEDSUBADDRESS", 0 },
	{ "CONNECTEDNUMBER", 0 },
	{ "CALLERHOLDID", 0 },
	{ "GOTO_ON_BLINDXFR", 0 },
	{ "TOUCH_MONITOR", 0 },
	{ "TOUCH_MONITOR_FORMAT", 0 },
	{ "DYNAMIC_FEATURES", 0 },
	{ "SPYGROUP", 0 },

	// ---------------------------------------------------------------------------
	// Built-in global variable names used in pbx.c
	// ---------------------------------------------------------------------------
	{ "EPOCH", 0 },
	{ "DATETIME", 0 },
	{ "TIMESTAMP", 0 },

	// ---------------------------------------------------------------------------
	// chan_agent variables
	// ---------------------------------------------------------------------------
	{ "AGENTMAXLOGINTRIES", 0 },
	{ "AGENTUPDATECDR", 0 },
	{ "AGENTGOODBYE", 0 },
	{ "AGENTACKCALL", 0 },
	{ "AGENTAUTOLOGOFF", 0 },
	{ "AGENTWRAPUPTIME", 0 },

	// ---------------------------------------------------------------------------
	// chan_misdn variables
	// ---------------------------------------------------------------------------
	{ "CRYPT_KEY", 0 },
	{ "MISDN_DIGITAL_TRANS", 0 },
	{ "MISDN_PID", 0 },

	// ---------------------------------------------------------------------------
	// chan_sip variables
	// ---------------------------------------------------------------------------
	{ "SIP_CODEC", 0 },
	{ "T38_DISABLE", 0 },
	{ "TRANSFER_CONTEXT", 0 },

	// ---------------------------------------------------------------------------
	// chan_unicall variables
	// ---------------------------------------------------------------------------
	{ "CALLING_PARTY_CATEGORY", 0 },

	// ---------------------------------------------------------------------------
	// chan_visdn variables
	// ---------------------------------------------------------------------------
	{ "BEARERCAP_RAW", 0 },
	{ "HLC_RAW", 0 },
	{ "LLC_RAW", 0 },

	// ---------------------------------------------------------------------------
	// chan_zap variables
	// ---------------------------------------------------------------------------
	{ "FEATDMF_CIC", 0 },
	{ "FEATDMF_OZZ", 0 },
	{ "PRITON", 0 },
	{ "PRILOCALTON", 0 },
	{ "USERUSERINFO", 0 },

	// ---------------------------------------------------------------------------
	// pbx_dundi variables
	// ---------------------------------------------------------------------------
	{ "ARG1", 0 },

	// ---------------------------------------------------------------------------
	// res_jabber variables
	// ---------------------------------------------------------------------------
	{ "STREAMFILE", 0 },

	// ---------------------------------------------------------------------------
	// res_js variables
	// ---------------------------------------------------------------------------
	{ "private_sound_dir", 0 },
	{ "JSFUNC", 0 },

	// ---------------------------------------------------------------------------
	// res_monitor variables
	// ---------------------------------------------------------------------------
	{ "AUTO_MONITOR_FORMAT", 0 },
	{ "AUTO_MONITOR_FNAME_BASE", 0 },
	{ "AUTO_MONITOR_FNAME_OPTS", 0 },

	// ---------------------------------------------------------------------------
	// Meetme variables
	// ---------------------------------------------------------------------------
	{ "MEETME_RECORDINGFILE", 0 },
	{ "MEETME_RECORDINGFORMAT", 0 },
	{ "MEETME_EXIT_CONTEXT", 0 },
	{ "MEETME_OGI_BACKGROUND", 0 },

	// ---------------------------------------------------------------------------
	// Monitor variables
	// ---------------------------------------------------------------------------
	{ "MONITOR_FILENAME", 0 },
	{ "MONITOR_EXEC", 0 },
	{ "MONITOR_EXEC_ARGS", 0 },

	// ---------------------------------------------------------------------------
	// NConference variables
	// ---------------------------------------------------------------------------
	{ "NCONF_OUTBOUND_CID_NAME", 0 },
	{ "NCONF_OUTBOUND_CID_NUM", 0 },
	{ "NCONF_OUTBOUND_CONTEXT", 0 },
	{ "NCONF_OUTBOUND_TIMEOUT", 0 },
	{ "NCONF_OUTBOUND_PARAMS", 0 },

	// ---------------------------------------------------------------------------
	// Proc variables
	// ---------------------------------------------------------------------------
	{ "PROC_RESULT", 0 },
	{ "PROC_DEPTH", 0 },
	{ "PROC_CONTEXT", 0 },
	{ "PROC_EXTEN", 0 },
	{ "PROC_PRIORITY", 0 },
	{ "PROC_OFFSET", 0 },

	// ---------------------------------------------------------------------------
	// Queue variables
	// ---------------------------------------------------------------------------
	{ "QUEUE_PRIO", 0 },

	// ---------------------------------------------------------------------------
	// TXFax/RXFax/T38gateway variables
	// ---------------------------------------------------------------------------
	{ "LOCALSTATIONID", 0 },
	{ "LOCALSUBADDRESS", 0 },
	{ "LOCALHEADERINFO", 0 },
	{ "FAX_DISABLE_V17", 0 },

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
