/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2003, Steve Underwood <steveu@coppice.org>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */
 
/*! \file
 *
 * \brief Text entry by DTMF
 * \ingroup applications
 *
 * \author Steve Underwood
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL", "$Revision: 41274 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <spandsp.h>

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/alaw.h"
#include "callweaver/callerid.h"

#define PRE_DIGIT_TIMEOUT   (8000*5)
#define INTER_DIGIT_TIMEOUT (8000*3/2)

static char *tdesc = "Assign entered string to a given variable";

static char *descrip =
"  DTMFToText(variable=initial digits|max chars|max time): Assigns a string\n"
"entered by someone, using DTMF.\n"
"\n"
"This provides functionality somewhat like text entry on a cellphone, but\n"
"works for any DTMF phone. It does not depend on the timing of the key taps, as\n"
"cellphones do. This would cause serious problems when the sending phone generates\n"
"DTMF, with timing completely isolated from the user's tapping of the keys (PBXs with\n"
"digital phones, cell phones to land lines, and other situations are generally like\n"
"this.\n"
"\n"
"Initially input is in numeric mode. The '*' and '#' keys are used to alter\n"
"the entry mode from that point, to permit full entry of English (or any\n"
"other Romance language that does not demand accents).\n"
"\n"
"'**X' changes mode, or backspaces. The valid values for 'X' are as follows:\n"
"\n"
"'**2' (C) backspaces a character\n"
"'**5' (L) selects lower case input\n"
"'**6' (N) selects numeric input\n"
"'**7' (P/S) selects punctuation/symbols\n"
"'**8' (U) selects upper case input\n"
"'**9' (W) backspaces a word\n"
"'**#' Read back message to date and continue entry\n"
"\n"
"When in alpha entry mode, characters are entered by multiple presses of the\n"
"numeric digit labelled with the required character. This is similar to text\n"
"entry on most cellphones.\n"
"'*' is a break point between characters, if it is not followed by a second '*'\n"
"'#' on its own terminates input\n"
"\n"
"In alpha mode, characters may be entered as follows:\n"
"0     ,    00    .    000   ?    0000  0\n"
"1     !    11    :    111   ;    1111  #    11111 1\n"
"2     A    22    B    222   C    2222  2\n"
"3     D    33    E    333   F    3333  3\n"
"4     G    44    H    444   I    4444  4\n"
"5     J    55    K    555   L    5555  5\n"
"6     M    66    N    666   O    6666  6\n"
"7     P    77    Q    777   R    7777  S    77777 7\n"
"8     T    88    U    888   V    8888  8\n"
"9     W    99    X    999   Y    9999  Z    99999 9\n"
"\n"
"In symbol mode, characters may be entered as follows:\n"
"0     =\n"
"1     <    11    (    111   [    1111  {    11111 1\n"
"2     @    22    $    222   &    2222  %    22222 2\n"
"3     >    33    )    333   ]    3333  }    33333 3\n"
"4     +    44    -    444   *    4444  /    44444 4\n"
"5     '    55    `    555   5\n"
"6     \"    66    6\n"
"7     ^    77    7\n"
"8     \\    88    |    888   8\n"
"9     _    99    ~    999   9\n";

static char *app = "DTMFToText";

static char *synopsis = "Text entry, by DTMF, to a given variable";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#if 0
#define FESTIVAL_CONFIG "festival.conf"

static char *socket_receive_file_to_buff(int fd, int *size)
{
    /* Receive file (probably a waveform file) from socket using   */
    /* Festival key stuff technique, but long winded I know, sorry */
    /* but will receive any file without closeing the stream or    */
    /* using OOB data                                              */
    static char *file_stuff_key = "ft_StUfF_key"; /* must == Festival's key */
    char *buff;
    int bufflen;
    int n;
    int k;
    int i;
    char c;

    bufflen = 1024;
    buff = (char *) malloc(bufflen);
    *size = 0;

    for (k = 0;  file_stuff_key[k] != '\0';  )
    {
        n = read(fd, &c, 1);
        if (n == 0)
            break;  /* hit stream eof before end of file */
        if ((*size) + k + 1 >= bufflen)
        {
            /* +1 so you can add a NULL if you want */
            bufflen += bufflen/4;
            buff = (char *) realloc(buff, bufflen);
        }
        if (file_stuff_key[k] == c)
        {
            k++;
        }
        else if ((c == 'X')  &&  (file_stuff_key[k + 1] == '\0'))
        {
            /* It looked like the key but wasn't */
            for (i = 0;  i < k;  i++, (*size)++)
                buff[*size] = file_stuff_key[i];
            k = 0;
            /* omit the stuffed 'X' */
        }
        else
        {
            for (i = 0;  i < k;  i++, (*size)++)
                buff[*size] = file_stuff_key[i];
            k = 0;
            buff[*size] = c;
            (*size)++;
        }
    }

    return buff;
}

static int send_waveform_to_fd(char *waveform, int length, int fd)
{
    int res;
    int x;

    res = fork();
    if (res < 0)
        opbx_log(LOG_WARNING, "Fork failed\n");
    if (res)
        return res;
    for (x = 0;  x < 256;  x++)
    {
        if (x != fd)
            close(x);
    }
	write(fd, waveform, length);
	write(fd, "a", 1);
	close(fd);
	exit(0);
}

static int send_waveform_to_channel(struct opbx_channel *chan, char *waveform, int length, char *intkeys)
{
	int res = 0;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int needed = 0;
	int owriteformat;
	struct opbx_frame *f;
	struct myframe
    {
		struct opbx_frame f;
		char offset[OPBX_FRIENDLY_OFFSET];
		char frdata[2048];
	} myf;
	
    if (pipe(fds))
    {
        opbx_log(LOG_WARNING, "Unable to create pipe\n");
        return -1;
    }
	                                                
	opbx_stopstream(chan);

	owriteformat = chan->writeformat;
	res = opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR);
	if (res < 0)
    {
		opbx_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		return -1;
	}
	
	res = send_waveform_to_fd(waveform, length, fds[1]);
	if (res >= 0)
    {
		pid = res;
		for (;;)
        {
			ms = 1000;
			res = opbx_waitfor(chan, ms);
			if (res < 1)
            {
				res = -1;
				break;
			}
			f = opbx_read(chan);
			if (!f)
            {
				opbx_log(LOG_WARNING, "Null frame == hangup() detected\n");
				res = -1;
				break;
			}
			if (f->frametype == OPBX_FRAME_DTMF)
            {
				opbx_log(LOG_DEBUG, "User pressed a key\n");
				if (intkeys && strchr(intkeys, f->subclass))
                {
					res = f->subclass;
					opbx_fr_free(f);
					break;
				}
			}
			else if (f->frametype == OPBX_FRAME_VOICE)
            {
				/* Treat as a generator */
				needed = f->samples * 2;
				if (needed > sizeof(myf.frdata))
                {
					opbx_log(LOG_WARNING,
                            "Only able to deliver %d of %d requested samples\n",
                            sizeof(myf.frdata)/2,
                            needed/2);
					needed = sizeof(myf.frdata);
				}
				res = read(fds[0], myf.frdata, needed);
				if (res > 0)
                {
					myf.f.frametype = OPBX_FRAME_VOICE;
					myf.f.subclass = OPBX_FORMAT_SLINEAR;
					myf.f.datalen = res;
					myf.f.samples = res / 2;
					myf.f.mallocd = 0;
					myf.f.offset = OPBX_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.data = myf.frdata;
					if (opbx_write(chan, &myf.f) < 0)
                    {
						res = -1;
						break;
					}
					if (res < needed)
                    {
                        // last frame
						opbx_log(LOG_DEBUG, "Last frame\n");
						res = 0;
						break;
					}
				}
                else
                {
					opbx_log(LOG_DEBUG, "No more waveform\n");
					res = 0;
				}
			}
			opbx_fr_free(f);
		}
	}
	close(fds[0]);
	close(fds[1]);
//	if (pid > -1)
//		kill(pid, SIGKILL);
	if (!res  &&  owriteformat)
		opbx_set_write_format(chan, owriteformat);
	return res;
}

#define MAXLEN 180
#define MAXFESTLEN 2048

static int festival_exec(struct opbx_channel *chan, char *vdata)
{
	int usecache;
	int res = 0;
	struct localuser *u;
 	struct sockaddr_in serv_addr;
	struct hostent *serverhost;
	int fd;
	FILE *fs;
	char *host;
	char *cachedir;
	char *temp;
	char *festivalcommand;
	int port=1314;
	int n;
	char ack[4];
	char *waveform;
	int filesize;
	int wave;
	char bigstring[MAXFESTLEN];
	int i;
	struct MD5Context md5ctx;
	unsigned char MD5Res[16];
	char MD5Hex[32];
	char koko[4];
	char cachefile[MAXFESTLEN];
	int readcache = 0;
	int writecache = 0;
	int strln; 
	int fdesc = -1;
	char buffer[16384];
	int seekpos = 0;	
	char data[256] = "";
	char *intstr;
	struct opbx_config *cfg;

	cfg = opbx_load(FESTIVAL_CONFIG);
	if (!cfg)
    {
		opbx_log(LOG_WARNING, "No such configuration file %s\n", FESTIVAL_CONFIG);
		return -1;
	}
	if (!(host = opbx_variable_retrieve(cfg, "general", "host")))
    {
		host = "localhost";
	}
	if (!(temp = opbx_variable_retrieve(cfg, "general", "port")))
    {
		port = 1314;
	}
    else
    {
		port = atoi(temp);
	}
	if (!(temp = opbx_variable_retrieve(cfg, "general", "usecache")))
    {
		usecache=0;
	}
    else
    {
		usecache = opbx_true(temp);
	}
	if (!(cachedir = opbx_variable_retrieve(cfg, "general", "cachedir")))
    {
		cachedir = "/tmp/";
	}
	if (!(festivalcommand = opbx_variable_retrieve(cfg, "general", "festivalcommand")))
    {
		festivalcommand = "(tts_textasterisk \"%s\" 'file)(quit)\n";
	}
	
	strncpy(data, vdata, sizeof(data) - 1);
	if ((intstr = strchr(data, '|')))
    {
		*intstr = '\0';
		intstr++;
		if (!strcasecmp(intstr, "any"))
			intstr = OPBX_DIGIT_ANY;
	}
	LOCAL_USER_ADD(u);
	opbx_log(LOG_DEBUG, "Text passed to festival server : %s\n",(char *)data);
	/* Connect to local festival server */
	
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
		opbx_log(LOG_WARNING,"festival_client: can't get socket\n");
        return -1;
	}
    memset(&serv_addr, 0, sizeof(serv_addr));
    if ((serv_addr.sin_addr.s_addr = inet_addr(host)) == -1)
    {
        /* its a name rather than an ipnum */
        serverhost = gethostbyname(host);
        if (serverhost == (struct hostent *) 0)
        {
            opbx_log(LOG_WARNING, "festival_client: gethostbyname failed\n");
            return -1;
        }
        memmove(&serv_addr.sin_addr, serverhost->h_addr, serverhost->h_length);
    }
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (connect(fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0)
    {
		opbx_log(LOG_WARNING,"festival_client: connect to server failed\n");
        return -1;
    }
    	
    /* Compute MD5 sum of string */
    MD5Init(&md5ctx);
    MD5Update(&md5ctx,(unsigned char const *) data, strlen(data));
    MD5Final(MD5Res, &md5ctx);
    strcpy(MD5Hex,"");
    	
    /* Convert to HEX and look if there is any matching file in the cache 
       directory */
    for (i = 0;  i < 16;  i++)
    {
        sprintf(koko, "%X", MD5Res[i]);
        strcat(MD5Hex, koko);
    }
    readcache = 0;
    writecache = 0;
    if (strlen(cachedir)+strlen(MD5Hex)+1<=MAXFESTLEN && (usecache==1))
    {
        sprintf(cachefile, "%s/%s", cachedir, MD5Hex);
        fdesc = open(cachefile, O_RDWR);
        if (fdesc == -1)
        {
            fdesc = open(cachefile, O_CREAT | O_RDWR,0);
            if (fdesc != -1)
            {
                writecache = 1;
                strln = strlen((char *) data);
                opbx_log(LOG_DEBUG, "line length : %d\n", strln);
                write(fdesc, &strln, sizeof(int));
                write(fdesc, data, strln);
                seekpos = lseek(fdesc, 0, SEEK_CUR);
                opbx_log(LOG_DEBUG, "Seek position : %d\n", seekpos);
            }
        }
        else
        {
            read(fdesc,&strln,sizeof(int));
            opbx_log(LOG_DEBUG, "Cache file exists, strln=%d, strlen=%d\n", strln, strlen((char *) data));
            if (strlen((char *) data) == strln)
            {
                opbx_log(LOG_DEBUG,"Size OK\n");
                read(fdesc,&bigstring,strln);
                if (strcmp(bigstring, data) == 0)
                { 
                    readcache = 1;
                }
                else
                {
                    opbx_log(LOG_WARNING, "Strings do not match\n");
                }
            }
            else
            {
                opbx_log(LOG_WARNING, "Size mismatch\n");
            }
        }
	}
    			
	if (readcache == 1)
    {
		close(fd);
		fd=fdesc;
		opbx_log(LOG_DEBUG, "Reading from cache...\n");
	}
    else
    {
		opbx_log(LOG_DEBUG, "Passing text to festival...\n");
        fs = fdopen(dup(fd), "wb");
		fprintf(fs, festivalcommand, (char *) data);
		fflush(fs);
		fclose(fs);
	}
	
	/* Write to cache and then pass it down */
	if (writecache == 1)
    {
		opbx_log(LOG_DEBUG, "Writing result to cache...\n");
		while ((strln = read(fd, buffer, 16384))!=0)
			write(fdesc, buffer, strln);
		close(fd);
		close(fdesc);
		fd=open(cachefile,O_RDWR);
		lseek(fd,seekpos,SEEK_SET);
	}
	
	opbx_log(LOG_DEBUG,"Passing data to channel...\n");
	
	/* Read back info from server */
	/* This assumes only one waveform will come back, also LP is unlikely */
	wave = 0;
	do
    {
		for (n = 0;  n < 3; )
			n += read(fd, ack + n, 3 - n);
		ack[3] = '\0';
		if (strcmp(ack, "WV\n") == 0)
        {
            /* Receive a waveform */
			opbx_log(LOG_DEBUG, "Festival WV command\n");
			waveform = socket_receive_file_to_buff(fd, &filesize);
			res = send_waveform_to_channel(chan, waveform, filesize, intstr);
			free(waveform);
			break;
		}
		else if (strcmp(ack, "LP\n") == 0)
        {
            /* Receive an s-expr */
			opbx_log(LOG_DEBUG, "Festival LP command\n");
			waveform = socket_receive_file_to_buff(fd, &filesize);
			waveform[filesize] = '\0';
			opbx_log(LOG_WARNING, "Festival returned LP : %s\n", waveform);
			free(waveform);
		}
        else if (strcmp(ack,"ER\n") == 0)
        {
            /* Server got an error */
			opbx_log(LOG_WARNING, "Festival returned ER\n");
			res = -1;
			break;
        }
	}
    while (strcmp(ack, "OK\n") != 0);
	close(fd);
	LOCAL_USER_REMOVE(u);                                                                                
	return res;

}
#endif

/* Makes words out of punctuation, to help TTS do a reasonable job of reading back the
   enetered text. */
static const char *char_to_text(char c)
{
    char *s;
    
    s = NULL;
    switch (c)
    {
    case ' ':
        s = "space";
        break;
    case '.':
        s = "period";
        break;
    case ',':
        s = "comma";
        break;
    case '!':
        s = "exclamation mark";
        break;
    case '?':
        s = "question mark";
        break;
    case '@':
        s = "\"at\" sign";
        break;
    }
    /*endswitch*/
    return  s;
}

enum
{
    TEXT_ENTRY_MODE_LOWER_CASE,
    TEXT_ENTRY_MODE_UPPER_CASE,
    TEXT_ENTRY_MODE_NUMERIC,
    TEXT_ENTRY_MODE_SYMBOL
};

static int get_input_text(struct opbx_channel *chan, const char *variable_name, const char *initial_digits, int max_chars, int max_time)
{
    static const char *uclc_presses[10] =
    {
        " ,.?0",
        "!:;#1",
        "ABC2",
        "DEF3",
        "GHI4",
        "JKL5",
        "MNO6",
        "PQRS7",
        "TUV8",
        "WXYZ9"
    };
    static const char *symbol_presses[10] =
    {
        "=0",
        "<([{1",
        "@$&%2",
        ">)]}3",
        "+-*/4",
        "'`5",
        "\"6",
        "^7",
        "\\|8",
        "_~9"
    };
    int res;
    int fest_res;
    int mode;
    int len;
    int hits;
    int i;
    int done;
    int total_timer;
    int timer;
    int digits;
    int new;
    char *s;
    char *t;
    char *u;
    char *ul;
    char *v;
    const char *q;
    const char *r;
    char x;
    char digval[128 + 1];
    char talk_back[1000 + 1];
    char entered_text[500 + 1];
	struct opbx_frame *f;
	dtmf_rx_state_t dtmf_state;
    int original_read_fmt;

    len = 0;
    t = entered_text;
    done = 0;
    mode = TEXT_ENTRY_MODE_NUMERIC;
    res = -1;
    fest_res = -1;
    timer = PRE_DIGIT_TIMEOUT;
    total_timer = 8000*max_time;
    digits = 0;
    if (initial_digits  &&  initial_digits[0])
    {
        strcpy(digval, initial_digits);
        digits = strlen(digval);
    }
    else
    {
        digval[0] = '\0';
        digits = 0;
    }
    /*endif*/

    original_read_fmt = chan->readformat;
    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR);
        if (res < 0)
        {
            opbx_log(LOG_WARNING, "Unable to set to linear read mode, giving up\n");
            return -1;
        }
        /*endif*/
    }
    /*endif*/
    dtmf_rx_init(&dtmf_state, NULL, NULL);
    while (!done  &&  opbx_waitfor(chan, -1) > -1)
    {
        f = opbx_read(chan);
        if (f == NULL)
        {
            opbx_log(LOG_WARNING, "Null frame == hangup() detected\n");
            res = -1;
            opbx_fr_free(f);
            break;
        }
        /*endif*/
        if (f->frametype == OPBX_FRAME_DTMF)
        {
            opbx_log(LOG_DEBUG, "User pressed '%c'\n", f->subclass);
            digval[digits++] = f->subclass;
            digval[digits] = '\0';
            if (f->subclass != '#')
            {
                /* Use a shorter timeout between digits */
                timer = INTER_DIGIT_TIMEOUT;
                opbx_fr_free(f);
                continue;
            }
            /*endif*/
        }
        else
        {
            if (f->frametype != OPBX_FRAME_VOICE)
            {
                opbx_fr_free(f);
                continue;
            }
            /*endif*/
            dtmf_rx(&dtmf_state, f->data, f->samples);

            /* Voice frames give us timing */
            timer -= f->samples;
            total_timer -= f->samples;

            new = dtmf_get(&dtmf_state, &digval[digits], 128 - digits);
            i = -1;
            if (new)
            {
                for (i = digits;  i < digits + new;  i++)
                {
                    if (digval[i] == '#')
                        break;
                    /*endif*/
                }
                /*endfor*/
                timer = INTER_DIGIT_TIMEOUT;
                digits += new;
            }
            /*endif*/
            if ((i < 0  ||  digval[i] != '#')  &&  timer > 0)
            {
                opbx_fr_free(f);
                continue;
            }
            /*endif*/
            //opbx_log(LOG_DEBUG, "Break\n");
        }
        /*endif*/
        timer = PRE_DIGIT_TIMEOUT;
        opbx_fr_free(f);

        opbx_log(LOG_WARNING, "Fresh digits '%s'\n", digval);
        if (digval[0] == '\0')
            break;
        /*endif*/
        
        /* Even if the caller hung up we may still have a valid input, as it
           is often valid to enter a string of digits at the last phase of a
           call and just drop the line */
        opbx_log(LOG_DEBUG, "Current text %d/%d\n", t - entered_text, max_chars);
        s = digval;
        ul =
        u = talk_back;
        while (*s  &&  !done)
        {
            x = *s++;
            hits = 1;
            while (*s == x)
            {
                s++;
                hits++;
            }
            /*endwhile*/
            //opbx_log(LOG_DEBUG, "%d of %c\n", hits, x);
            if (x == '*')
            {
                switch (hits)
                {
                case 1:
                    /* This is just a break marker, so ignore it */
                    //opbx_log(LOG_DEBUG, "Marker - ignore\n");
                    break;
                case 2:
                    /* The next character should define a new mode or
                       a delete operation */
                    //opbx_log(LOG_DEBUG, "Selector - '%c'\n", *s);
                    switch (*s)
                    {
                    case '2':
                        s++;
                        /* Backspace */
                        if (t > entered_text)
                        {
                            t--;
                            u += sprintf(u, "delete ");
                            r = char_to_text(*t);
                            if (r)
                                u += sprintf(u, "%s, ", r);
                            else
                                u += sprintf(u, "%c, ", *t);
                            /*endif*/
                        }
                        /*endif*/
                        break;
                    case '5':
                        s++;
                        mode = TEXT_ENTRY_MODE_LOWER_CASE;
                        break;
                    case '6':
                        s++;
                        mode = TEXT_ENTRY_MODE_NUMERIC;
                        break;
                    case '7':
                        s++;
                        mode = TEXT_ENTRY_MODE_SYMBOL;
                        break;
                    case '8':
                        s++;
                        mode = TEXT_ENTRY_MODE_UPPER_CASE;
                        break;
                    case '9':
                        s++;
                        /* Backspace over word */
                        if (t > entered_text)
                        {
                            u += sprintf(u, "delete whole word, ");
                            t--;
                            while (t > entered_text  &&  *t == ' ')
                                t--;
                            /*endwhile*/
                            while (t > entered_text  &&  *t != ' ')
                                t--;
                            /*endwhile*/
                            if (*t == ' ')
                                t++;
                            /*endif*/
                        }
                        /*endif*/
                        break;
                    case '#':
                        s++;
                        /* Read back text to date, and continue entry */
                        u = talk_back;
                        *t = '\0';
                        u += sprintf(u, "%s", entered_text);
                        break;
                    }
                    /*endswitch*/
                    break;
                default:
                    /* Too many stars - treat this as a marker, like 1 star */
                    //opbx_log(LOG_DEBUG, "Marker(like) - ignore\n");
                    break;
                }
                /*endswitch*/
            }
            else if (x == '#')
            {
                /* Terminate text entry */
                //opbx_log(LOG_DEBUG, "Hash\n");
                *u = '\0';
                *t = '\0';
                done = 1;
            }
            else if (isdigit(x))
            {
                //opbx_log(LOG_DEBUG, "Digit - %d of %c\n", hits, x);
                switch (mode)
                {
                case TEXT_ENTRY_MODE_LOWER_CASE:
                case TEXT_ENTRY_MODE_UPPER_CASE:
                    q = uclc_presses[x - '0'];
                    q += ((hits - 1)%strlen(q));
                    if (mode == TEXT_ENTRY_MODE_LOWER_CASE)
                        *t = tolower(*q);
                    else
                        *t = *q;
                    /*endif*/
                    if (*t == ' ')
                    {
                        /* End of word? */
                        if (t > entered_text  &&  *(t - 1) != ' ')
                        {
                            u = ul;
                            v = t;
                            while (v > entered_text  &&  *v == ' ')
                                v--;
                            /*endwhile*/
                            while (v > entered_text  &&  *v != ' ')
                                v--;
                            /*endwhile*/
                            while (v <= t)
                                *u++ = *v++;
                            /*endwhile*/
                            ul = u;
                        }
                        else
                        {
                            u += sprintf(u, "space, ");
                        }
                        /*endif*/
                    }
                    else
                    {
                        r = char_to_text(*t);
                        if (r)
                            u += sprintf(u, "%s, ", r);
                        else
                            u += sprintf(u, "%c, ", *t);
                        /*endif*/
                    }
                    /*endif*/
                    break;
                case TEXT_ENTRY_MODE_NUMERIC:
                    for (i = 1;  i < hits;  i++)
                    {
                        *t++ = x;
                        u += sprintf(u, "%c, ", x);
                    }
                    /*endfor*/
                    *t = x;
                    u += sprintf(u, "%c, ", x);
                    break;
                case TEXT_ENTRY_MODE_SYMBOL:
                    q = symbol_presses[x - '0'];
                    q += ((hits - 1)%strlen(q));
                    if (mode == TEXT_ENTRY_MODE_LOWER_CASE)
                        *t = tolower(*q);
                    else
                        *t = *q;
                    /*endif*/
                    u += sprintf(u, "%c, ", *t);
                    break;
                }
                /*endif*/
                t++;
                if ((t - entered_text) >= max_chars)
                    done = 1;
                /*endif*/
            }
            else
            {
                /* Bad character! (perhaps an A-D). Ignore it */
            }
            /*endif*/
        }
        /*endwhile*/
        *u =
        *t = '\0';

        if (done  ||  total_timer <= 0)
        {
            res = 0;
            break;
        }
        /*endif*/

        opbx_log(LOG_WARNING, "Text so far '%s'\n", entered_text);
        opbx_log(LOG_WARNING, "Entered '%s'\n", talk_back);
        digval[0] = '\0';
        digits = 0;
        timer = PRE_DIGIT_TIMEOUT;
#if 0
        if ((fest_res = festival_exec(chan, talk_back)) < 0)
            break;
        /*endif*/
        if (fest_res > 0)
        {
            strcpy(digval, initial_digits);
            digits = strlen(digval);
            timer = INTER_DIGIT_TIMEOUT;
        }
        /*endif*/
#endif
    }
    /*endwhile*/
    opbx_log(LOG_WARNING, "Entered text: \"%s\"\n", entered_text);
    pbx_builtin_setvar_helper(chan, (char *) variable_name, entered_text);
    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        res = opbx_set_read_format(chan, original_read_fmt);
        if (res)
            opbx_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
        /*endif*/
    }
    /*endif*/
    return  res;
}

static int dtmftotext_exec(struct opbx_channel *chan, void *data)
{
    char *variable_name;
    char *initial_digits;
    char *max_chars;
    char *max_time;
    char *stringp;
    int imax_chars;
    int imax_time;
    int res;
    struct localuser *u;
    
    LOCAL_USER_ADD(u);
    res = 0;
    stringp = alloca(strlen(data) + 1);
    strncpy(stringp, data, strlen(data) + 1);
    if (strchr(stringp, '|')  &&  strchr(stringp, '='))
    {
        variable_name = strsep(&stringp, "=");
        initial_digits = strsep(&stringp, "|");
        max_chars = strsep(&stringp, "|");
        max_time = strsep(&stringp, "\0");
        if (variable_name == NULL  ||  initial_digits == NULL  ||  max_chars == NULL  ||  max_time == NULL)
        {
            opbx_log(LOG_WARNING, "Ignoring, since there is no argument: variable, initial digits, max chars, or timeout\n");
        }
        else
        {
            imax_chars = atoi(max_chars);
            imax_time = atoi(max_time);
            if (variable_name  &&  initial_digits)
            {
                if (chan->_state != OPBX_STATE_UP)
                {
                    /* Shouldn't need this, but checking to see if channel is already answered
                     * Theoretically asterisk should already have answered before running the app */
                    res = opbx_answer(chan);
                }
                /*endif*/
                if (res == 0)
                    res = get_input_text(chan, variable_name, initial_digits, imax_chars, imax_time);
                /*endif*/
            }
            /*endif*/
        }
        /*endif*/
    }
    else
    {
        opbx_log(LOG_WARNING, "Ignoring, no parameters\n");
    }
    /*endif*/
    opbx_log(LOG_WARNING, "Done!\n");
	LOCAL_USER_REMOVE(u);
    return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, dtmftotext_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);
	return res;
}
