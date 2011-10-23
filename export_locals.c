/*
 * export_locals.c - Extract Local Symbols from a ELF file.
 *
 * Created : Thu Nov 11 17:24:15 2004
 *
 * Copyright (C) 2004, Sriram Karra <karra@shakti.homelinux.net>.  All
 * Rights Reserved. 
 *
 * $Id$
 *
 * L I C E N S E
 * =============
 * 
 *     This program is distributed under the terms of GNU General Public
 *     License v2.  Note - this program depends on libbfd, which is
 *     itself GPLed (*not* LGPL), and as such one doesn't have much
 *     choice in the matter of licensing of derivative works.
 *
 *     I would like make special mention of the following fact: During
 *     the period this program was developed I was employed by HCL
 *     Technologies India Ltd., working as a consultant for Cisco Inc.
 *     However this program was written completely in my free time on my
 *     home computer and as such no resources (time/computers/etc.) of
 *     either of the two mentioned companies were used for development
 *     purposes.
 *
 * Introduction
 *
 *     By definition, symbols marked 'static' are not accessible outside
 *     the compilation unit.  This is just as well.  However there are
 *     certain applications where we might want linker access to exactly
 *     these shy chappies.  This is a hack alright; but trust me, there
 *     are 'genuine' applications for such use.
 *
 *     This program extracts a specified set of static symbols from a
 *     given object file's symbol table and outputs them to a new binary
 *     object file.  The new binary can be linked with other object
 *     files thereby providing linkage access to once-static symbols of
 *     a program.  Duplicate static symbols should be resolved using
 *     some user-provided, or, alternately, some sensible default
 *     heuristics.
 *
 * Usage
 *
 *     ./export_locals <input_obj_file> -syms a,b,c [-all] [-out <filename>]
 *     
 *     Where the symbols a, b, c, d etc. are to be extracted and
 *     exported.  The output elf file `foo'.
 *     
 *     Since we are dealing with symbols having local scope, it is
 *     possile for multiple symbols to be present with the same name.
 *     For now we handle this case by selecting the 'first' symbol we
 *     encounter.  More sophisticated things may be made available in
 *     future.
 *
 *     If the -out flag is not specified, the extracted symbols will be
 *     written to a filename called 'foo'.
 * 
 * Status:
 *
 * Nov 27, 2004:
 *
 *     There is a small problem with the address of the symbols after
 *     the output file is linked with an external file.  The output file
 *     contains the right address, but after linking with another file,
 *     the address of the externally linked symbol differs.  Need to
 *     verify if this is really a problem or not.  Certainly looks like
 *     a problem... But let's see.
 *
 * Potential Issues
 *
 *     This has been tested with libbfd-2.15.  It is bound to have
 *     issues with older versions.  You _could_ run into certain type
 *     mismatch issues with older versions of bfd as certain types have
 *     been made long long in recent versions.
 *
 *     This has been tested only on ELF object files.  The whole point
 *     of bfd being portability across obejct formats, I would expect
 *     this stuff to 'just work' for coff/a.out. But do not sue me if
 *     doesn't.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bfd.h>
#include <assert.h>

/*
 * This is a dummy symbol; useful to test this program on self and try
 * to extract this dummy chap out as a global
 */
static int dummy_local_sym;

static const char *brew_usage_str =
"./export_locals <input_obj_file> -syms a,b,c [-all] [-out <filename>]";

const int ALL_LOCAL_SYMS = -1;

static struct parsed_args_ {
    char *inp_filename;
    char *out_filename;
    int  n_syms;		/* No. of symbols requested for
				 * exporting.  -1 means all of them */
    char **sym_names;    
    int  debug_level;
} brew_arg_info;

static int
brew_is_local_symbol (bfd *b, asymbol *a)
{
    flagword f;

    f = a->flags;
    return ((f & BSF_LOCAL) == BSF_LOCAL
	    && !bfd_is_local_label(b, a));
}

static int
brew_get_local_sym_count (bfd *b, asymbol **sym_tab)
{
    int      i, ret;
    long     nsym;
    asymbol  *a;

    bfd_set_error(bfd_error_no_error);

    nsym = bfd_canonicalize_symtab(b, sym_tab);
    if (bfd_get_error()) {
	bfd_perror("Could not canonicalize sym tab in get_local_sym_count: ");
    }

    if (!nsym) {
	printf("Right... something hosed.  nsym.\n");
    } else {
	printf("%ld symbols found\n", nsym);
    }
 
    for (i = 0, ret = 0; i < nsym; ++i) {
	a = sym_tab[i];
	if (brew_is_local_symbol(b, a)) {
	    ++ret;
	}
    }

    return (ret);
}

static int
brew_seen_symbol_before (const char **names, const char *name, int *size)
{
    int i;

    for (i = 0; i < *size; ++i) {
	if (strcmp(names[i], name) == 0) {
	    return (1);
	}
    }

    names[(*size)++] = name;
    return (0);
}

/*
 * Returns the number of symbols.  Caller should free *ptrs.
 */
static int
brew_extract_locals_as_globals (bfd *b, bfd *out, asymbol ***ptrs,
				char **syms, int n_syms, int debug)
{
    int 	  i, j, nnames, found;
    long 	  stn, nsym, nlocals, l;
    asymbol 	 *a, *new, **sym_tab;
    asection 	 *sec;
    const char  **names;
    const char 	 *sec_name;

    bfd_set_error(bfd_error_no_error);

    stn     = bfd_get_symtab_upper_bound(b);
    sym_tab = malloc(stn);
    nsym    = bfd_canonicalize_symtab(b, sym_tab);
    names   = malloc(nsym * sizeof(char*));

    if (bfd_get_error()) {
	bfd_perror("Could not canonicalize sym tab in extract_locals: ");
    }

    nlocals = brew_get_local_sym_count(b, sym_tab);
    *ptrs   = malloc((nlocals + 1) * sizeof(asymbol*));
     
    for (i = 0, l = 0, nnames = 0; i < nsym; ++i) {
	a = sym_tab[i];

	if (!brew_is_local_symbol(b, a))
	    continue;

	found = 0;
	for (j = 0; j < n_syms; ++j) {
	    if (strcmp(a->name, syms[j]) == 0) {
		found = 1;
		break;
	    }
	}

	if (!found && (n_syms != ALL_LOCAL_SYMS))
	    continue;

	sec_name = bfd_get_section_name(b, a->section);
	assert(sec_name);
	sec = bfd_get_section_by_name(out, sec_name);
	if (!sec) {
	    if (debug) {
		printf("No sec. for section name: %s.  "
		       "Skipping symbol '%s'\n", sec_name, a->name);
	    }
	    continue;
	}

	if (!brew_seen_symbol_before(names, a->name, &nnames)) {
	    new 	 = bfd_make_empty_symbol(out);
	    new->name    = a->name;
	    new->section = sec;
	    new->flags   = BSF_GLOBAL;
	    new->value   = a->value;
	    (*ptrs)[l++] = new;
	} else if (debug) {
	    printf("Duplicate static symbol %s.  Discarding value %llx\n",
		   a->name, a->value);
	}
	    
    }

    (*ptrs)[l] = 0;
    free(sym_tab);
    free(names);

    return (l);
}

/*
 * In 'out', create all the sections that are available in 'b'
 */
static void
brew_copy_section_names (bfd *b, bfd *out)
{
    asection *s, *o;

    bfd_set_error(bfd_error_no_error);

#if 0
    nsecs = bfd_count_sections(b);
    if (bfd_get_error()) {
	bfd_perror("bfd_count_sections failed in copy_section_names: ");
	return;
    }
#endif

    for (s = b->sections; s; s = s->next) {
	o = bfd_make_section_old_way(out, bfd_get_section_name(b, s));
	o->user_set_vma = 1;
	o->vma = s->vma;
    }
}

/*
 * This routine walks through the symbol table of given file and
 * produces a new ELF file with those exported as globals
 *
 * syms    - A list if symbol names to export.
 * n_syms  - The number of syms.  If 0, all the local symbols found will
 *           be exported.
 */
static void
brew_export_locals (bfd *b, int n_syms, char **syms, char *out_file,
		    int debug)
{
    bfd 	 *out;
    asymbol 	**ptrs;
    long 	  nsyms;

    out = bfd_openw(out_file, bfd_get_target(b));
    if (!out) {
	bfd_perror("bfd_openw failed in brew_export_locals: ");
	return;
    }
    bfd_set_format(out, bfd_object);

    brew_copy_section_names(b, out);
    nsyms = brew_extract_locals_as_globals(b, out, &ptrs, syms, n_syms, debug);

    bfd_set_symtab(out, ptrs, nsyms);
    bfd_close(out);

    free(ptrs);
}

static void
brew_list_matching_formats (char **p)
{
    fprintf(stderr, "brewrite: Matching formats: ");
    while (*p) {
	fprintf(stderr, " %s", *p++);
    }

    fputc('\n', stderr);
}

/*
 * The following three routines were useful initially when I was just
 * trying to find my way around bfd.  Keeping it around for later
 * reference.
 */
#ifdef GENERAL_DEBUGGING_CODE

static void brew_show_sections (bfd*)     __attribute__ ((unused));
static void brew_show_symbols (bfd*)      __attribute__ ((unused));
static void brew_print_target_list (void) __attribute__ ((unused));

static void
brew_show_sections (bfd *b)
{
    int        i, nsecs;
    asection   *s;

    bfd_set_error(bfd_error_no_error);

    nsecs = bfd_count_sections(b);
    printf("No. of sections = %d\n", nsecs);
    if (bfd_get_error()) {
	bfd_perror("count sections failed in show_sections: ");
    }

    for (s = b->sections, i = 0; s; s = s->next, ++i) {
	printf("Section [%2d] = %s\n", i, bfd_get_section_name(b, s));
    }

    assert(i == nsecs);
}

static void
brew_show_symbols (bfd *b)
{
    int       	 i;
    long      	 stn, nsym = 0;
    asymbol   	*a, **sym_tab;
    asection  	*s;
    flagword  	 f;

    bfd_set_error(bfd_error_no_error);

    // It has been noticed that the the .symtab section is not
    // 'available', but the symbols themselves are available...
    // wierd...
    s = bfd_get_section_by_name(b, ".symtab");
    if (!s) {
	printf(".symtab not found here dude... not sure why\n");
    }

    stn     = bfd_get_symtab_upper_bound(b);
    sym_tab = malloc(stn);
    nsym    = bfd_canonicalize_symtab(b, sym_tab);
    if (bfd_get_error()) {
	bfd_perror("Could not canonicalize sym tab in show_symbols: ");
    }

    if (!nsym) {
	printf("Right... something hosed.  nsym.\n");
    } else {
	printf("%ld symbols found\n", nsym);
    }
 
    for (i = 0; i < nsym; ++i) {
	a = sym_tab[i];
	f = a->flags;
	if ((f & BSF_LOCAL) == BSF_LOCAL
	    && !bfd_is_local_label(b, a)) {
	    printf("%-25s %#8llx %s\n",
		   bfd_asymbol_name(a),
		   bfd_asymbol_value(a),
		   bfd_get_section_name(b, bfd_get_section(a)));
	}
    }

    free(sym_tab);
}

/*
 * Take a look at the available target names.  On my Debian machine at
 * home, this is what I got when I just executed this routine.  One of
 * these strings need to be passed as the second argument for
 * bfd_open.() routines.
 *
 * Target  1 = elf32-i386
 * Target  2 = a.out-i386-linux
 * Target  3 = elf32-little
 * Target  4 = elf32-big
 * Target  5 = srec
 * Target  6 = symbolsrec
 * Target  7 = tekhex
 * Target  8 = binary
 * Target  9 = ihex
 * Target 10 = trad-core
 */
static void
brew_print_target_list (void)
{
    int   i;
    const char **ts;

    ts = bfd_target_list();

    if (ts) {
	i = 0;
	while (*ts) {
	    printf("Target %2d = %s\n", ++i, *ts);
	    ts++;
	}
    } else {
	printf("Null target list.. duh\n");
    }
}

#endif // GENERIC_DEBUGGING_CODE

static void
print_args (struct parsed_args_ *a)
{
    int i;

    if (a->out_filename) {
	printf("output filename = %s\n", a->out_filename);
    }

    printf("No. of input symbols   = %d\n", a->n_syms);
    printf("Requested symbols = ");
    for (i = 0; i < a->n_syms; ++i) {
	printf("%s ", a->sym_names[i]);
    }
    printf("\n");
}

/*
 * Parses the args and populates the fields of 'a' appropriately.
 */
static void
parse_args (int argc, char *argv[], struct parsed_args_ *a)
{
    int 	 i;
    char 	*s;
    void 	*v;

    a->inp_filename = NULL;
    a->n_syms 	    = 0;
    a->sym_names    = malloc(1*sizeof(char*));
    a->sym_names[0] = NULL;
    a->out_filename = NULL;
    a->debug_level  = 0;

    for (i = 1; i < argc; ++i) {
	if (strcmp(argv[i], "-syms") == 0) {
	    s = strtok(argv[++i], ", ");
	    while (s) {
		v = realloc(a->sym_names, (a->n_syms + 1)*sizeof(char*));
		if (!v) {
		    fprintf(stderr, "Out of memeory in %s\n", __FUNCTION__);
		    abort();
		}

		a->sym_names = v;
		a->sym_names[a->n_syms] = malloc(strlen(s) + 1);
		strcpy(a->sym_names[a->n_syms++], s);

		s = strtok(NULL, ", ");
	    }
	} else if (strcmp(argv[i], "-all") == 0) {
	    a->n_syms = ALL_LOCAL_SYMS;
	} else if (strcmp(argv[i], "-out") == 0) {
	    if (i == argc) {
		fprintf(stderr, "-out flag requires output filename\n");
		abort();
	    }

	    a->out_filename = argv[++i];
	} else if (strcmp(argv[i], "-debug") == 0) {
	    a->debug_level = 1;
	} else {
	    a->inp_filename = malloc(strlen(argv[i]) + 1);
	    strcpy(a->inp_filename, argv[i]);
	}
    }

    if (!a->n_syms) {
	fprintf(stderr, "Should specify either -syms or -all\n");
	fprintf(stderr, "Usage: %s\n", brew_usage_str);
	abort();
    }

    if (!a->inp_filename) {
	fprintf(stderr, "Input object filename not specified\n");
	fprintf(stderr, "Usage: %s\n", brew_usage_str);
	abort();
    }

    if (!a->out_filename) {
	fprintf(stderr, "No output filename specified.  Using `foo'\n");
	a->out_filename = "foo";
    }
}

int
main (int argc, char *argv[])
{
    bfd    	*b;
    char   	**matching;

    parse_args(argc, argv, &brew_arg_info);
    print_args(&brew_arg_info);

    bfd_init();
    if (bfd_get_error()) {
	bfd_perror("bfd_init failed in main()");
    }

    b = bfd_openr(brew_arg_info.inp_filename, NULL);
    if (!b) {
	bfd_perror("Failed to open input file in main()");
	return (1);
    }

    /*
     * The following call is, for as yet unknown reason, quite crucial.
     * Things just do not work otherwise.
     */
    if (!bfd_check_format_matches(b, bfd_object, &matching)) {
	if (bfd_get_error() == bfd_error_file_ambiguously_recognized) {
	    fprintf(stderr,
		    "Ambigous input file.  BFD confused \n");
	    brew_list_matching_formats(matching);
	    free(matching);
	} else {
	    bfd_perror("Wierd error during format check in main()");
	}

	return (1);
    }

    brew_export_locals(b, brew_arg_info.n_syms, brew_arg_info.sym_names,
		       brew_arg_info.out_filename, brew_arg_info.debug_level);
//     brew_show_symbols(b);
//     brew_show_sections(b);

    bfd_close(b);
    return (dummy_local_sym);
}
