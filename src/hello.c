#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include <stdio.h>
#include "builtins.h"
#include "shell.h"
#include "bashgetopt.h"
#include "common.h"
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#include "base64simple.h"
#define BASE64_ENCODED_COUNT 4
#define BASE64_DECODED_COUNT 3

/*
 * Input/Output Structure Definition
 */
typedef struct {
	char encoded[BASE64_ENCODED_COUNT];
	unsigned char decoded[BASE64_DECODED_COUNT];
	size_t index;
	size_t error;
} base64;

/*
 * Base64 Character Encoding Table
 */
static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
/*
 * The base64simple_encode_chars() function encodes the characters stored
 * in the decoded[] character array member of the base64 structure. Only
 * the number of characters specified by index will be encoded. Encoded
 * characters are stored in the encoded[] character array member of the
 * returned base64 structure.
 */
static base64 base64simple_encode_chars(base64 data) {
	uint32_t octet_1, octet_2, octet_3;
	uint32_t combined = 0;
	
	// Assigning octets
	octet_1 = data.index >= 1 ? data.decoded[0] : 0;
	octet_2 = data.index >= 2 ? data.decoded[1] : 0;
	octet_3 = data.index >= 3 ? data.decoded[2] : 0;

	// Combine octets into a single 32 bit int
	combined = (octet_1 << 16) + (octet_2 << 8) + octet_3;
	
	// Generate encoded chars
	data.encoded[0] = encoding_table[(combined >> 18) & 0x3F];
	data.encoded[1] = encoding_table[(combined >> 12) & 0x3F];
	data.encoded[2] = encoding_table[(combined >> 6) & 0x3F];
	data.encoded[3] = encoding_table[(combined >> 0) & 0x3F];
	
	// Setting trailing chars '=' in accordance with base64 encoding standard
	if (data.index == 1) {
		data.encoded[2] = '=';
		data.encoded[3] = '=';
	} else if (data.index == 2) {
		data.encoded[3] = '=';
	}
	
	return data;
}

/*
 * The base64simple_decode_chars() function decodes the characters stored
 * in the encoded[] character array member of the base64 structure. Decoded
 * characters are stored in the decoded[] character array member of the
 * returned base64 structure.
 */
static base64 base64simple_decode_chars(base64 data) {
	size_t   i, f = 0;
	uint32_t octet_1, octet_2, octet_3, octet_4;
	uint32_t combined = 0;
	
	// Set the index to the decode count and decrement for each '=' we find.
	// This tells the calling function how many decoded characters are valid.
	data.index = BASE64_DECODED_COUNT;
	
	// Change encoded chars to decimal index in encoding_table
	for (i = 0; i < 64; ++i)
		if (data.encoded[0] == encoding_table[i]) {
			data.encoded[0] = i;
			++f;
			break;
		}
	for (i = 0; i < 64; ++i)
		if (data.encoded[1] == encoding_table[i]) {
			data.encoded[1] = i;
			++f;
			break;
		}
	for (i = 0; i < 64; ++i)
		if (data.encoded[2] == encoding_table[i]) {
			data.encoded[2] = i;
			++f;
			break;
		} else if (data.encoded[2] == '=') {
			data.encoded[2] = 0;

			// Make sure the next char is also a '='. Otherwise don't
			// increment f and return an error below.
			if (data.encoded[3] == '=') {
				--(data.index);
				++f;
			}

			break;
		}
	for (i = 0; i < 64; ++i)
		if (data.encoded[3] == encoding_table[i]) {
			data.encoded[3] = i;
			++f;
			break;
		} else if (data.encoded[3] == '=') {
			data.encoded[3] = 0;
			--(data.index);
			++f;
			break;
		}
	
	// Verify all input chars were found, return with error if not
	if (f < 4) {
		data.error = 1;
		return data;
	}
	
	// Assigning octets
	octet_1 = data.encoded[0];
	octet_2 = data.encoded[1];
	octet_3 = data.encoded[2];
	octet_4 = data.encoded[3];
	
	// Combine octets into a single 32 bit int
	combined = (octet_1 << 18) + (octet_2 << 12) + (octet_3 << 6) + octet_4;
	
	data.decoded[0] = (combined >> 16) & 0xFF;
	data.decoded[1] = (combined >> 8) & 0xFF;
	data.decoded[2] = (combined >> 0) & 0xFF;
	
	return data;
}

/*
 * This function is a simple interface for the base64simple_encode_chars()
 * function defined above. Client programs are meant to use this function
 * instead of using base64simple_encode_chars() directly. It takes a pointer
 * to a character array and the array size, and returns a pointer to a
 * null-terminated string containing the encoded result.
 */
char *base64simple_encode(unsigned char *a, size_t s) {
	size_t i, j, l;
	base64 contents = { .index = 0 };
	char *r;
	
	// Calculating size of return string and allocating memory
	if (s % BASE64_DECODED_COUNT == 0)
		r = malloc(((s / BASE64_DECODED_COUNT) * BASE64_ENCODED_COUNT) + 1);
	else
		r = malloc((((s / BASE64_DECODED_COUNT) + 1) * BASE64_ENCODED_COUNT) + 1);
	
	// Check for a successful malloc
	if (r == NULL)
		return NULL;
	
	// Loop over input string and encoding the contents
	for (l = 0, i = 0; i < s; ++i) {
		contents.decoded[contents.index++] = a[i];
		if (contents.index == BASE64_DECODED_COUNT) {
			contents = base64simple_encode_chars(contents);
			for (j = 0; j < BASE64_ENCODED_COUNT; ++j, ++l) {
				r[l] = contents.encoded[j];
			}
			r[l] = '\0';
			contents.index = 0;
		}
	}
	if (contents.index > 0) {
		contents = base64simple_encode_chars(contents);
		for (j = 0; j < BASE64_ENCODED_COUNT; ++j, ++l) {
			r[l] = contents.encoded[j];
		}
		r[l] = '\0';
	}
	
	return r;
}

/*
 * This function is a simple interface for the base64simple_decode_chars()
 * function defined above. Client programs are meant to use this function
 * instead of using base64simple_decode_chars() directly. It takes a pointer
 * to a string and returns the decoded version, also as a pointer to a string.
 * If a decode error occures, a NULL pointer is returned.
 */
unsigned char *base64simple_decode(char *a, size_t s, size_t *rs) {
	size_t i, j, l;
	base64 contents = { .index = 0, .error = 0 };
	unsigned char *r;
	
	// Calculating size of return string and allocating memory
	if (s % BASE64_ENCODED_COUNT == 0)
		r = malloc((s / BASE64_ENCODED_COUNT) * BASE64_DECODED_COUNT);
	else
		return NULL;
	
	// Check for a successful malloc
	if (r == NULL)
		return NULL;
	
	// Loop over input string and decoding the contents
	for (l = 0, i = 0; i < s; ++i) {
		contents.encoded[contents.index++] = a[i];
		if (contents.index == BASE64_ENCODED_COUNT) {
			contents = base64simple_decode_chars(contents);

			// Invalid encoding. Break out of loop.
			if (contents.error)
				break;

			// Append decoded characters to return string
			for (j = 0; j < contents.index; ++j, ++l)
				r[l] = contents.decoded[j];
			
			// If we encountered any '=' signs we reached the signature
			// for the end of a base64 string. Break out of loop.
			if (contents.index < BASE64_DECODED_COUNT)
				break;
				
			contents.index = 0;
		}

	}
	
	// Return NULL if there was a decode error. Otherwise, return decoded
	// string and store its length.
	if (contents.error) {
		*rs = 0;
		return NULL;
	} else {
		*rs = l;
		return r;
	}
}

#define INIT_DYNAMIC_VAR(var, val, gfunc, afunc) \
  do \
    { SHELL_VAR *v = bind_variable (var, (val), 0); \
      v->dynamic_value = gfunc; \
      v->assign_func = afunc; \
    } \
  while (0)

static SHELL_VAR *
assign_epochrealtime (
     SHELL_VAR *self,
     char *value,
     arrayind_t unused,
     char *key )
{
  return (self);
}


static SHELL_VAR *
get_epochrealtime (SHELL_VAR *var)
{
  struct timeval tv;
  char *output = malloc(18);

  gettimeofday(&tv, NULL);
  sprintf (output, "%d.%d", tv.tv_sec, tv.tv_usec);

  FREE (value_cell (var));

  var_setvalue (var, output);
  return (var);
}

int
enable_epochrealtime_builtin(WORD_LIST *list)
{
  INIT_DYNAMIC_VAR ("EPOCHREALTIME", (char *)NULL, get_epochrealtime, assign_epochrealtime);
  INIT_DYNAMIC_VAR ("EPOCHREALTIME1", (char *)NULL, get_epochrealtime, assign_epochrealtime);




char *decoded, *encoded;
	size_t i, size, r_size;

	// Encoding
	decoded = "This is a decoded string.";
	size = strlen(decoded);
	encoded = base64simple_encode(decoded, size);
	if (encoded == NULL) {
		printf("Insufficient Memory!\n");
	} else {
		printf("Encoded: %s\n", encoded);
	}

	// Decoding
	size = strlen(encoded);
	decoded = base64simple_decode(encoded, size, &r_size);
	if (decoded == NULL) {
		printf("Improperly Encoded String or Insufficient Memory!\n");
	} else {
		printf("Decoded: %s\n", decoded);
	}

	free(encoded);
	free(decoded);


  return 0;
}

char const *enable_epochrealtime_doc[] = {
  "Enable $EPOCHREALTIME.",
  "",
  "Time since the epoch, as returned by gettimeofday(2), formatted as decimal",
  "tv_sec followed by a dot ('.') and tv_usec padded to exactly six decimal digits.",
  (char *)0
};


/* A builtin `xxx' is normally implemented with an `xxx_builtin' function.
   If you're converting a command that uses the normal Unix argc/argv
   calling convention, use argv = make_builtin_argv (list, &argc) and call
   the original `main' something like `xxx_main'.  Look at cat.c for an
   example.

   Builtins should use internal_getopt to parse options.  It is the same as
   getopt(3), but it takes a WORD_LIST *.  Look at print.c for an example
   of its use.

   If the builtin takes no options, call no_options(list) before doing
   anything else.  If it returns a non-zero value, your builtin should
   immediately return EX_USAGE.  Look at logname.c for an example.

   A builtin command returns EXECUTION_SUCCESS for success and
   EXECUTION_FAILURE to indicate failure. */
int
hello_builtin (list)
     WORD_LIST *list;
{
  printf("hello world\n");
  fflush (stdout);
  enable_epochrealtime_builtin(list);
  return (EXECUTION_SUCCESS);
}

int
hello_builtin_load (s)
     char *s;
{
  printf ("hello builtin loaded.........\n");
  fflush (stdout);
  return (1);
}

void
hello_builtin_unload (s)
     char *s;
{
  printf ("hello builtin unloaded\n");
  fflush (stdout);
}

/* An array of strings forming the `long' documentation for a builtin xxx,
   which is printed by `help xxx'.  It must end with a NULL.  By convention,
   the first line is a short description. */
char *hello_doc[] = {
	"Sample builtin.",
	"",
	"this is the long doc for the sample hello builtin",
	(char *)NULL
};

/* The standard structure describing a builtin command.  bash keeps an array
   of these structures.  The flags must include BUILTIN_ENABLED so the
   builtin can be used. */
struct builtin hello_struct = {
	"hello",		/* builtin name */
	hello_builtin,		/* function implementing the builtin */
	BUILTIN_ENABLED,	/* initial flags for builtin */
	hello_doc,		/* array of long documentation strings. */
	"hello",		/* usage synopsis; becomes short_doc */
	0			/* reserved for internal use */
};



