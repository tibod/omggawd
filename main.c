/*
Genetic Algorithm WTF Decisionator
(c) Greg Toombs 2013 (should I really put my name on this?)
*/

#include <assert.h>
#include <fcntl.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <json/json.h>

regex_t rexgoodline, rexvar, rexstupid, rexsuicide;

void clean(char *content) {
	// xml is yucky
	static const char *replacements[][2] = {
		{ "&lt;&lt;",
		  "<<      " },
		{ "&lt;",
		  "<   "  },
		{ "&gt;&gt;",
		  ">>      " },
		{ "&gt;",
		  ">   "  },
		{ "&amp;&amp;",
		  "&&        " },
		{ "&amp;",
		  "&    "  }
	};

	for (int i = 0; i < sizeof(replacements)/sizeof(*replacements); i++) {
		char *target = content;
		for (;;) {
			char *dest = strstr(target, replacements[i][0]);
			if (!dest) break;
			const char *better = replacements[i][1];
			int len = strlen(better);
			strncpy(dest, better, len);
			target += len;
		}
	}
}

bool compile(const char *awesomecode) {
	FILE *source = fopen("awesome.c", "w");
	assert(source);
	fputs(
		"int x = -1;\n"
		"int awesomerand() {\n",
		source);
	fputs(awesomecode, source);
	fputs(
		"\n"
		"return x;\n"
		"}\n", source);
	fclose(source);
	int result = system(
		"gcc -o libawesome.so awesome.c -fpic -shared -nostdinc -nostdlib "
		"2>/dev/null");
	return !result;
}

FILE *potentials;

void munch(char *content) {
	for (int i = 0;;) {
		// i've done found me a new line
		char *begin = content + i;
		while (content[i] >= ' ') i++;
		bool die = !content[i];
		content[i] = 0;
		
		// Like magic: no loops, no control statements, and all variables are
		// renamed to "x"
		regmatch_t match;
		
		
		int result = regexec(&rexgoodline, begin, 0, 0, 0);
		if (result == REG_NOERROR &&
			!strstr(begin, "for") &&
			!strstr(begin, "while") &&
			!strstr(begin, "char") &&
			!strstr(begin, "int") &&
			!strstr(begin, "float") &&
			!strstr(begin, "double") &&
			(
				// I want stuff to be bigger
				strstr(begin, "+") ||
				strstr(begin, "*") ||
				strstr(begin, "<<") ||
				strstr(begin, "%")
			)) {
			for (char *word = begin;; word += match.rm_eo) {
				result = regexec(&rexvar, word, 1, &match, 0);
				if (result != REG_NOERROR)
					break;
				word[match.rm_so] = 'x';
				for (int j = match.rm_so + 1; j < match.rm_eo; j++)
					word[j] = ' ';
			}
			
			if (compile(begin)) {
				bool stupid = REG_NOERROR == regexec(&rexstupid, begin, 0,0,0),
					suicide = REG_NOERROR == regexec(&rexsuicide, begin, 0,0,0); 
				if (!stupid && !suicide) {
					printf("Munched '%s'\n", begin);
			
					fputs(begin, potentials);
					fputc('\n', potentials);
				}
			}
		}
		
		if (die) return;
		
		while (content[++i] < ' ') {
			if (!content[i]) return;
		}
	}
}

static int codeindex = 0;
static void parseBody(char *body) {
	static const char btag[] = "<code>", etag[] = "</code>";
	int oldindex = codeindex;

	for (;; codeindex++) {
		// Parse out a single code nugget
		char *begin = strstr(body, btag);
		if (!begin) break;
		char *content = begin + sizeof(btag) - 1;
		char *end = strstr(content, etag);
		if (!end) break;
		*end = '\0';
		clean(content);
		munch(content);

		body = end + sizeof(etag) - 1;
	}

	printf("Parsed code nuggets: %d new, %d total\n",
		codeindex - oldindex, codeindex);
}

static CURLcode curlerr(CURLcode errornum, const char *cmd) {
	if (errornum != CURLE_OK)
		fprintf(stderr, "cURL error %d from %s: %s\n",
			errornum, cmd, curl_easy_strerror(errornum));
	return errornum;
}
#define CURLERR(cmd) curlerr(cmd, #cmd)

// Stream in from cURL to the JSON parser
static struct json_tokener *jtok;
static struct json_object *jo;
static size_t writecurl(char *ptr, size_t size, size_t nmemb, void *userdata) {
	size_t newbytes = size*nmemb;

	jo = json_tokener_parse_ex(jtok, ptr, newbytes);
	enum json_tokener_error jerr = json_tokener_get_error(jtok);
	if (!((jo && jerr == json_tokener_success) ||
		 (!jo && jerr == json_tokener_continue))) {
		fprintf(stderr, "\nJSON error %d: %s\n", 
			jerr, json_tokener_error_desc(jerr));
		return 0;
	}
	return newbytes;
}

// jSON field helper
#define JO(field, type, jotype) \
type field = 0; { \
	struct json_object *innerjo = json_object_object_get(ojo, #field); \
	if (innerjo) \
		field = json_object_get_##jotype(innerjo); \
}

#define JO_INT(field) JO(field, int, int)
#define JO_STR(field) JO(field, const char*, string)

static CURL *curl;
static bool getCode(int page, int pagesize, const char *tag) {
	json_tokener_reset(jtok);

	char url[1024];
	snprintf(url, sizeof(url),
		"http://api.stackexchange.com/2.1/search/advanced?"
		"page=%d&pagesize=%d&tagged=%s&"
		"site=stackoverflow&filter=!g)vqRDxpLR(dV",
		page, pagesize, tag);
		
	assert(!CURLERR(curl_easy_setopt(curl, CURLOPT_URL, url)));
	if (CURLERR(curl_easy_perform(curl)))
		return true;
	
	struct json_object *ojo = jo;

	JO_INT(error_id);
	if (error_id) {
		JO_STR(error_name);
		JO_STR(error_message);
		fprintf(stderr, "\nSO JSON error %d: %s (%s)\n",
			error_id, error_name, error_message);
		return false;
	}

	JO_INT(total);
	JO_INT(quota_remaining);
	JO_INT(quota_max);
	JO_INT(backoff);
	JO_STR(has_more);
	bool bhas_more = !strcmp("true", has_more);

	printf("page %d; items %d-%d of %d; "
		"quota %d/%d (%d%%); backoff %d; %s more\n", // \r
		page,
		1 + (page - 1)*pagesize, page*pagesize, total,
		quota_remaining, quota_max, quota_remaining*100/quota_max,
		backoff,
		has_more ? "has" : "no");
	fflush(stdout);

	struct json_object *jitems = json_object_object_get(jo, "items");
	if (jitems) {
		int nitems = json_object_array_length(jitems);
		for (int i = 0; i < nitems; i++) {
			ojo = json_object_array_get_idx(jitems, i);
			JO_STR(body);
			// const? pffft
			parseBody((char*)body);
		}
	}

	json_object_put(jo); // put = release, who knows why

	if (!bhas_more || !quota_remaining)
		return false;
	if (backoff)
		sleep(backoff);
	return true;
}

void spew(const char *str) {
	FILE *resultsjs = fopen("results.js", "w");
	fprintf(resultsjs, "var message = '%s';", str);
	fclose(resultsjs);
}

int main(int argc, char **argv) {
	spew("Please wait while I fetch some randomization code off of the internet.");
	system("sensible-browser ./omgui.html &");

	curl = curl_easy_init();
	assert(curl);
	assert(!CURLERR(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecurl)));
	assert(!CURLERR(curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip")));

	jtok = json_tokener_new();
	assert(jtok);
	
	
	assert(!regcomp(&rexgoodline, "^[^=}{[]+=[^=}{[]+$",
		REG_EXTENDED));
	assert(!regcomp(&rexvar, "[_A-Za-z][_A-Za-z0-9]*",
		REG_EXTENDED));
	assert(!regcomp(&rexstupid, "^ *x *= *[0-9'\" ]+ *;",
		REG_EXTENDED));
	assert(!regcomp(&rexsuicide, "^ *x *\\^= *x *;",
		REG_EXTENDED));
	potentials = fopen("potentials.c", "w");
	
	// Only use up 50 of the SO quota that's usually 300
	for (int p = 1; getCode(p, 10, "c") && p < 10; p++);

	json_tokener_free(jtok);
	curl_easy_cleanup(curl);
	fclose(potentials);
	
	spew("Okay, I have some code. Please wait while I get ready for your question.");
	
	// Potentials file is filled, so now we set the processor on fire
	return system("mpirun -np `nproc` ./ga-r");
}
