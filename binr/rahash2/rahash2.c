/* radare - LGPL - Copyright 2009-2016 - pancake */

#include <stdio.h>
#include <string.h>
#include <getopt.c>
#include <r_io.h>
#include <r_hash.h>
#include <r_util.h>
#include <r_print.h>
#include <r_crypto.h>
#include "../blob/version.c"

static ut64 from = 0LL;
static ut64 to = 0LL;
static int incremental = 1;
static int iterations = 0;
static int quiet = 0;
static RHashSeed s = {0}, *_s = NULL;

void compare_hashes(const RHash *ctx, const ut8 *compare, int length, int *ret) {
	if (compare) {
		// algobit has only 1 bit set
		if (!memcmp (ctx->digest, compare, length)) {
			printf ("rahash2: Computed hash matches the expected one.\n");
		} else {
			eprintf ("rahash2: Computed hash doesn't match the expected one.\n");
			*ret = 1;
		}
	}
}

static void do_hash_seed(const char *seed) {
	const char *sptr = seed;
	if (!seed) {
		_s = NULL;
		return;
	}
	_s = &s;
	s.buf = (ut8*)malloc (strlen (seed) + 128);
	if (!s.buf) {
		_s = NULL;
		return;
	}
	if (*seed=='^') {
		s.prefix = 1;
		sptr++;
	} else s.prefix = 0;
	if (!strncmp (sptr, "s:", 2)) {
		strcpy ((char*)s.buf, sptr + 2);
		s.len = strlen (sptr + 2);
	} else {
		s.len = r_hex_str2bin (sptr, s.buf);
		if (s.len < 1) {
			strcpy ((char*)s.buf, sptr);
			s.len = strlen (sptr);
		}
	}
}

static void do_hash_hexprint (const ut8 *c, int len, int ule, int rad) {
	int i;
	if (ule) {
		for (i = len - 1; i >= 0; i--)
			printf ("%02x", c[i]);
	} else {
		for (i = 0; i < len; i++)
			printf ("%02x", c[i]);
	}
	if (rad != 'j')
		printf ("\n");
}

static void do_hash_print(RHash *ctx, int hash, int dlen, int rad, int ule) {
	char *o;
	const ut8 *c = ctx->digest;
	const char *hname = r_hash_name (hash);
	switch (rad) {
	case 0:
		if (!quiet)
			printf ("0x%08"PFMT64x"-0x%08"PFMT64x" %s: ",
				from, to > 0 ? to - 1 : 0, hname);
		do_hash_hexprint (c, dlen, ule, rad);
		break;
	case 1:
		printf ("e file.%s=", hname);
		do_hash_hexprint (c, dlen, ule, rad);
		break;
	case 'n':
		do_hash_hexprint (c, dlen, ule, 'j');
		break;
	case 'j':
		printf ("{\"name\":\"%s\",\"hash\":\"", hname);
		do_hash_hexprint (c, dlen, ule, rad);
		printf ("\"}");
		break;
	default:
		o = r_print_randomart (c, dlen, from);
		printf ("%s\n%s\n", hname, o);
		free (o);
		break;
	}
}

static int do_hash_internal(RHash *ctx, int hash, const ut8 *buf, int len, int rad, int print, int le) {
	int dlen;
	if (len < 0)
		return 0;
	dlen = r_hash_calculate (ctx, hash, buf, len);
	if (!dlen) return 0;
	if (!print) return 1;
	if (hash == R_HASH_ENTROPY) {
		double e = r_hash_entropy (buf, len);
		if (rad) {
			eprintf ("entropy: %10f\n", e);
		} else {
			printf ("0x%08"PFMT64x"-0x%08"PFMT64x" %10f: ",
					from, to > 0 ? to - 1 : 0, e);
			r_print_progressbar (NULL, 12.5 * e, 60);
			printf ("\n");
		}
	} else {
		if (iterations > 0)
			r_hash_do_spice (ctx, hash, iterations, _s);
		do_hash_print (ctx, hash, dlen, rad, le);
	}
	return 1;
}

static int do_hash(const char *file, const char *algo, RIO *io, int bsize, int rad, int ule, const ut8 *compare) {
	ut64 j, fsize, algobit = r_hash_name_to_bits (algo);
	RHash *ctx;
	ut8 *buf;
	int ret = 0;
	int i, first = 1;
	if (algobit == R_HASH_NONE) {
		eprintf ("rahash2: Invalid hashing algorithm specified\n");
		return 1;
	}
	fsize = r_io_size (io);
	if (fsize < 1) {
		eprintf ("rahash2: Invalid file size\n");
		return 1;
	}
	if (bsize < 0) bsize = fsize / -bsize;
	if (bsize == 0 || bsize > fsize) bsize = fsize;
	if (to == 0LL) to = fsize;
	if (from > to) {
		eprintf ("rahash2: Invalid -f -t range\n");
		return 1;
	}
	if (fsize == -1LL) {
		eprintf ("rahash2: Unknown file size\n");
		return 1;
	}
	buf = malloc (bsize + 1);
	if (!buf)
		return 1;
	ctx = r_hash_new (R_TRUE, algobit);

	if (rad == 'j')
		printf ("[");
	if (incremental) {
		for (i = 1; i < 0x800000; i <<= 1) {
			if (algobit & i) {
				int hashbit = i & algobit;
				int dlen = r_hash_size (hashbit);
				r_hash_do_begin (ctx, i);
				if (rad == 'j') {
					if (first) {
						first = 0;
					} else {
						printf (",");
					}
				}
				if (s.buf && s.prefix) {
					do_hash_internal (ctx,
						hashbit, s.buf, s.len, rad, 0, ule);
				}
				for (j = from; j < to; j+= bsize) {
					int len = ((j + bsize) > to) ? (to - j) : bsize;
					r_io_pread (io, j, buf, len);
					do_hash_internal (ctx, hashbit, buf,
						len, rad, 0, ule);
				}
				if (s.buf && !s.prefix) {
					do_hash_internal (ctx, hashbit, s.buf,
						s.len, rad, 0, ule);
				}
				r_hash_do_end (ctx, i);
				if (iterations > 0)
					r_hash_do_spice (ctx, i, iterations, _s);
				if (!*r_hash_name (i))
					continue;
				if (!quiet && rad != 'j') {
					printf ("%s: ", file);
				}
				do_hash_print (ctx, i, dlen, quiet?'n':rad, ule);
				if (quiet == 1) {
					printf (" %s\n", file);
				} else printf ("\n");
			}
		}
		if (_s)
			free (_s->buf);
	} else {
		/* iterate over all algorithm bits */
		if (s.buf)
			eprintf ("Warning: Seed ignored on per-block hashing.\n");
		for (i = 1; i < 0x800000; i <<= 1) {
			ut64 f, t, ofrom, oto;
			if (algobit & i) {
				int hashbit = i & algobit;
				ofrom = from;
				oto = to;
				f = from;
				t = to;
				for (j = f; j < t; j += bsize) {
					int nsize = (j + bsize < fsize) ? bsize : (fsize - j);
					r_io_pread (io, j, buf, bsize);
					from = j;
					to = j + bsize;
					if (to > fsize)
						to = fsize;
					do_hash_internal (ctx, hashbit, buf, nsize, rad, 1, ule);
				}
				from = ofrom;
				to = oto;
			}
		}
	}
	if (rad == 'j')
		printf ("]\n");

	compare_hashes (ctx, compare, r_hash_size (algobit), &ret);
	r_hash_free (ctx);
	free (buf);
	return ret;
}

static int do_help(int line) {
	printf ("Usage: rahash2 [-rBhLkv] [-b S] [-a A] [-c H] [-E A] [-s S] [-f O] [-t O] [file] ...\n");
	if (line) return 0;
	printf (
	" -a algo     comma separated list of algorithms (default is 'sha256')\n"
	" -b bsize    specify the size of the block (instead of full file)\n"
	" -B          show per-block hash\n"
	" -c hash     compare with this hash\n"
	" -e          swap endian (use little endian)\n"
	" -E algo     encrypt (rc4 for now). Use -S to set key\n"
	" -d / -D     encode/decode base64 string (-s) or file to stdout\n"
	" -f from     start hashing at given address\n"
	" -i num      repeat hash N iterations\n"
	" -S seed     use given seed (hexa or s:string) use ^ to prefix (key for -E)\n"
	" -k          show hash using the openssh's randomkey algorithm\n"
	" -q          run in quiet mode (-qq to show only the hash)\n"
	" -L          list all available algorithms (see -a)\n"
	" -r          output radare commands\n"
	" -s string   hash this string instead of files\n"
	" -t to       stop hashing at given address\n"
	" -x hexstr   hash this hexpair string instead of files\n"
	" -v          show version information\n");
	return 0;
}

static void algolist() {
	ut64 bits;
	int i;
	for (i = 0; ; i++) {
		bits = ((ut64)1) << i;
		const char *name = r_hash_name (bits);
		if (!name || !*name) break;
		printf ("%s\n", name);
	}
}

#define setHashString(x,y) {\
	if (hashstr) { \
		eprintf ("Hashstring already defined\n");\
		return 1; \
	}\
	hashstr_hex = y; \
	hashstr = x;\
}

int is_power_of_two(const ut64 x) {
	return x && !(x & (x - 1));
}

int main(int argc, char **argv) {
	int i, ret, c, rad = 0, bsize = 0, numblocks = 0, ule = 0, b64mode = 0;
	const char *algo = "sha256"; /* default hashing algorithm */
	const char *seed = NULL;
	const char *encrypt = NULL;
	char *hashstr = NULL;
	const char *compareStr = NULL;
	ut8 *compareBin = NULL;
	int hashstr_len = -1;
	int hashstr_hex = 0;
	ut64 algobit;
	RHash *ctx;
	RIO *io;

	while ((c = getopt (argc, argv, "jdDrveE:a:i:S:s:x:b:nBhf:t:kLqc:")) != -1) {
		switch (c) {
		case 'q': quiet ++; break;
		case 'i':
			iterations = atoi (optarg);
			if (iterations < 0) {
				eprintf ("error: -i argument must be positive\n");
				return 1;
			}
			break;
		case 'j': rad = 'j'; break;
		case 'S': seed = optarg; break;
		case 'n': numblocks = 1; break;
		case 'd': b64mode = 1; break;
		case 'D': b64mode = 2; break;
		case 'E': encrypt = optarg; break;
		case 'L': algolist (); return 0;
		case 'e': ule = 1; break;
		case 'r': rad = 1; break;
		case 'k': rad = 2; break;
		case 'a': algo = optarg; break;
		case 'B': incremental = 0; break;
		case 'b': bsize = (int)r_num_math (NULL, optarg); break;
		case 'f': from = r_num_math (NULL, optarg); break;
		case 't': to = 1 + r_num_math (NULL, optarg); break;
		case 'v': return blob_version ("rahash2");
		case 'h': return do_help (0);
		case 's': setHashString (optarg, 0); break;
		case 'x': setHashString (optarg, 1); break;
		case 'c': compareStr = optarg; break;
		default: return do_help(0);
		}
	}
	if (compareStr) {
		int compareBin_len;
		if (bsize && !incremental) {
			eprintf ("rahash2: Option -c incompatible with -b and -B options.\n");
			return 1;
		}
		if (b64mode) {
			eprintf ("rahash2: Option -c incompatible with -d or -D options.\n");
			return 1;
		}
		algobit = r_hash_name_to_bits(algo);
		// if algobit represents a single algorithm then it's a power of 2
		if (!is_power_of_two (algobit)) {
			eprintf ("rahash2: Option -c incompatible with multiple algorithms in -a.\n");
			return 1;
		}
		compareBin = malloc ((strlen (compareStr) + 1) * 2);
		if (!compareBin)
			return 1;
		compareBin_len = r_hex_str2bin (compareStr, compareBin);
		if (compareBin_len < 1) {
			eprintf ("rahash2: Invalid -c hex hash\n");
			free (compareBin);
			return 1;
		}
		else if (compareBin_len != r_hash_size (algobit)) {
			eprintf (
				"rahash2: Given -c hash has %d bytes but the selected algorithm returns %d bytes.\n",
				compareBin_len,
				r_hash_size(algobit));
			free (compareBin);
			return 1;
		}
	}
	if ((st64)from >= 0 && (st64)to < 0) {
		to = 0; // end of file
	}
	if (from || to) {
		if (to && from >= to) {
			eprintf ("Invalid -f or -t offsets\n");
			return 1;
		}
	}
	do_hash_seed (seed);
	if (hashstr) {
#define INSIZE 32768
		ret = 0;
		if (!strcmp (hashstr, "-")) {
			int res = 0;
			hashstr = malloc (INSIZE);
			if (!hashstr)
				return 1;
			res = fread ((void*)hashstr, 1, INSIZE - 1, stdin);
			if (res < 1) res = 0;
			hashstr[res] = '\0';
			hashstr_len = res;
		}
		if (hashstr_hex) {
			ut8 *out = malloc ((strlen (hashstr) + 1) * 2);
			hashstr_len = r_hex_str2bin (hashstr, out);
			if (hashstr_len < 1) {
				eprintf ("Invalid hex string\n");
				free (out);
				return 1;
			}
			hashstr = (char *)out;
			/* out memleaks here, hashstr can't be freed */
		} else {
			hashstr_len = strlen (hashstr);
		}
		if (from) {
			if (from>=hashstr_len) {
				eprintf ("Invalid -f.\n");
				return 1;
			}
		}
		if (to) {
			if (to>hashstr_len) {
				eprintf ("Invalid -t.\n");
				return 1;
			}
		} else {
			to = hashstr_len;
		}
		hashstr = hashstr + from;
		hashstr_len = to - from;
		hashstr[hashstr_len] = '\0';
		hashstr_len = r_str_unescape (hashstr);
		if (encrypt) {
			int seedlen = seed? strlen (seed): 0;
			if (seedlen > 0) {
				RCrypto *cry = r_crypto_new ();
				if (r_crypto_use (cry, encrypt)) {
					ut8 *binseed = malloc (seedlen + 1);
					if (binseed) {
						int len = r_hex_str2bin (seed, binseed);
						if (len <1) {
							len = seedlen;
							strcpy ((char *)binseed, seed);
						} else {
							seedlen = len;
						}
						if (r_crypto_set_key (cry, binseed, seedlen, 0, 0)) {
							const char *buf = hashstr;
							int buflen = hashstr_len;

							r_crypto_update (cry, (const ut8*)buf, buflen);
							r_crypto_final (cry, NULL, 0);

							int result_size = 0;
							ut8 *result = r_crypto_get_output (cry, &result_size);
							if (result) {
								write (1, result, result_size);
								free (result);
							}
						} else {
							eprintf ("Invalid key\n");
						}

						free (binseed);
						return 0;
					} else {
						eprintf ("Cannot allocate %d bytes\n", seedlen);
					}
				} else {
					eprintf ("Unknown encryption algorithm '%s'\n", encrypt);
				}
				r_crypto_free (cry);
			} else {
				eprintf ("Encryption key not defined. Use -S [key]\n");
			}
			return 1;
		} else
		switch (b64mode) {
		case 1: // encode
			{
			char *out = malloc (((hashstr_len + 1) * 4) / 3);
			if (out) {
				r_base64_encode (out, (const ut8*)hashstr, hashstr_len);
				printf ("%s\n", out);
				fflush (stdout);
				free (out);
			}
			}
			break;
		case 2: // decode
			{
			ut8 *out = malloc (INSIZE);
			if (out) {
				int outlen = r_base64_decode (out,
					(const char *)hashstr, hashstr_len);
				write (1, out, outlen);
				free (out);
			}
			}
			break;
		default:
			{
				char *str = (char *)hashstr;
				int strsz = hashstr_len;
				if (_s) {
					// alloc/concat/resize
					str = malloc (strsz + s.len);
					if (s.prefix) {
						memcpy (str, s.buf, s.len);
						memcpy (str+s.len, hashstr, hashstr_len);
					} else {
						memcpy (str, hashstr, hashstr_len);
						memcpy (str+strsz, s.buf, s.len);
					}
					strsz += s.len;
					str[strsz] = 0;
				}
				algobit = r_hash_name_to_bits (algo);
				for (i = 1; i < 0x800000; i <<= 1) {
					if (algobit & i) {
						int hashbit = i & algobit;
						ctx = r_hash_new (R_TRUE, hashbit);
						from = 0;
						to = strsz;
						do_hash_internal (ctx, hashbit,
							(const ut8*)str, strsz, rad, 1, ule);
						compare_hashes (ctx, compareBin,
							r_hash_size (algobit), &ret);
						r_hash_free (ctx);
					}
				}
				if (_s) {
					free (str);
					free (s.buf);
				}
			}
		}
		return ret;
	}
	if (optind >= argc)
		return do_help (1);
	if (numblocks) {
		bsize = -bsize;
	} else if (bsize < 0) {
		eprintf ("rahash2: Invalid block size\n");
		return 1;
	}

	io = r_io_new ();
	for (ret = 0, i = optind; i < argc; i++) {
		switch (b64mode) {
		case 1: // encode
			{
			int binlen;
			char *out;
			ut8 *bin = (ut8*)r_file_slurp (argv[i], &binlen);
			if (!bin) {
				eprintf ("Cannot open file\n");
				continue;
			}
			out = malloc (((binlen + 1) * 4) / 3);
			if (out) {
				r_base64_encode (out, bin, binlen);
				printf ("%s\n", out);
				fflush (stdout);
				free (out);
			}
			free (bin);
			}
			break;
		case 2: // decode
			{
			int binlen, outlen;
			ut8 *out, *bin = (ut8*)r_file_slurp (argv[i], &binlen);
			if (!bin) {
				eprintf ("Cannot open file\n");
				continue;
			}
			out = malloc (binlen + 1);
			if (out) {
				outlen = r_base64_decode (out, (const char*)bin, binlen);
				write (1, out, outlen);
				free (out);
			}
			free (bin);
			}
			break;
		default:
			if (!strcmp (argv[i], "-")) {
				int sz = 0;
				ut8 *buf = (ut8*)r_stdin_slurp (&sz);
				char *uri = r_str_newf ("malloc://%d", sz);
				if (sz > 0) {
					if (!r_io_open_nomap (io, uri, 0, 0)) {
						eprintf ("rahash2: Cannot open malloc://1024\n");
						return 1;
					}
					r_io_pwrite (io, 0, buf, sz);
				}
				free (uri);
			} else {
				if (r_file_is_directory (argv[i])) {
					eprintf ("rahash2: Cannot hash directories\n");
					return 1;
				}
				if (!r_io_open_nomap (io, argv[i], 0, 0)) {
					eprintf ("rahash2: Cannot open '%s'\n", argv[i]);
					return 1;
				}
			}
			ret |= do_hash (argv[i], algo, io, bsize, rad, ule, compareBin);
		}
	}
	free (hashstr);
	r_io_free (io);

	return ret;
}
