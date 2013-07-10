/* fixes broken TIFF Files
 * 
 * fixes invalid DateTime-field in Baseline-TIFFs,
 * based on http://www.awaresystems.be/imaging/tiff/tifftags/baseline.html
 *
 * author: Andreas Romeyke, 2013
 * licensed under conditions of libtiff 
 */


#include "fixit_tiff.h"

/** 20 comes from TIFF definition 
 */
#define TIFFDATETIMELENGTH 20


/** global variables */
static int flag_be_verbose=FLAGGED;
static int flag_check_only=UNFLAGGED;

/** help function */
void help () {
  printf ("fixit_tiff\n");
  printf ("call it with:\n");
  printf ("\tfixit_tiff [-h|-c|-s] -i <infile> [-o <outfile>]\n");
  printf ("\nwhere <infile> is the possibly broken file\n");
  printf ("and <outfile> is the name of the corrected file\n");
}

/** copy infile to outfile 
 * @param inf string with infile name 
 * @param outf string with outfile name 
 */
void copy_file (const char * inf, const char * outf) {
  FILE * in = fopen( inf, "rb");
  if (NULL == in) {
    fprintf(stderr, "could not open file '%s' for reading\n", inf);
    exit (FIXIT_TIFF_READ_PERMISSION_ERROR);
  }
  FILE * out = fopen(outf, "wb");
  if (NULL == out) {
    fprintf(stderr, "could not open file '%s' for writing\n", outf);
    exit (FIXIT_TIFF_WRITE_PERMISSION_ERROR);
  }
  /* copy infile to outfile */
  char            buffer[512];
  size_t          n;
  while ((n = fread(buffer, sizeof(char), sizeof(buffer), in)) > 0)
  {
    if (n != fwrite(buffer, sizeof(char), n, out)) {
      fprintf(stderr, "failure writing to file '%s'\n", outf);
      exit (FIXIT_TIFF_WRITE_ERROR);
    }
  }
  fclose(in);
  fclose(out);
}

/** check if date / time values are within correct ranges 
 * @param year year
 * @param month month
 * @param day day
 * @param hour hour
 * @param min min
 * @param sec sec
 * @return 0 if success, otherwise -1
 */
int test_plausibility (int * year, int * month, int * day, int * hour, int * min, int * sec) {
  if (FLAGGED == flag_be_verbose) printf ("found: y=%d m=%d d=%d h=%d m=%d s=%d\n", *year, *month, *day, *hour, *min, *sec);
  if (
      1500 < *year && 
      2100 > *year &&
      0 < *month &&
      13 > *month &&
      0 < *day &&
      32 > *day &&
      0 <= *hour &&
      24 > *hour &&
      0 <= *min &&
      60 > *min &&
      0 <= *sec &&
      60 > *sec
     ) {
    return 0;
  } else {
    return -1;
  }
}

/** RULE0: default rule (string is correct) */
int rule_default (char * datestring, int * year, int * month, int * day, int * hour, int * min, int * sec) {
  if (FLAGGED == flag_be_verbose) printf ("rule00\n");
  if (6 == sscanf(datestring, "%04d:%02d:%02d%02d:%02d:%02d", year, month, day, hour, min, sec)) {
    return test_plausibility(year, month, day, hour, min, sec);
  } else {
    return -2;
  }
}

/** RULE1: fix: '04.03.2010 09:59:17' => '04:03:2010 09:59:17' */
int rule_ddmmyyhhmmss_01 (char * datestring, int * year, int * month, int * day, int * hour, int * min, int * sec) {
  if (FLAGGED == flag_be_verbose) printf ("rule01\n");
  if (6 == sscanf(datestring, "%02d.%02d.%04d%02d:%02d:%02d", day, month, year, hour, min, sec)) {
    return test_plausibility(year, month, day, hour, min, sec);
  } else {
    return -2;
  }
}

/** RULENOFIX: dummy rule if no other rule matches, calls only exit */
int rule_nofix (char * datestring, int * year, int * month, int * day, int * hour, int * min, int * sec) {
  fprintf(stderr, "rule nofix, there is no applyable rule left, aborted without fixing problem\n");
  exit(FIXIT_TIFF_DATETIME_RULE_NOT_FOUND);
}

/** used for array of rules */
#define COUNT_OF_RULES 3
/** Array of rules */
int (*rules_ptr[COUNT_OF_RULES])(char *, int *, int *, int *, int *, int *, int *) = {
  rule_default,
  rule_ddmmyyhhmmss_01,
  rule_nofix
};

/** corrects broken date string to expected format, see 
 * http://www.awaresystems.be/imaging/tiff/tifftags/datetime.html
 * @param broken_datetime string with wrong datetime
 * @return string with corrected datetime
 */
char * correct_datestring (char * broken_datetime) {
  int day;
  int month;
  int year;
  int hour;
  int min;
  int sec;
  /* if ret is wrong, you could try another rules to apply */
  int r;
  for (r = 0; r < COUNT_OF_RULES; r++) {
    if (FLAGGED == flag_be_verbose) printf("Applying rule%i", r);
    if (0 != (*rules_ptr[r])(broken_datetime, &year, &month, &day, &hour, &min, &sec)) {
      if (FLAGGED == flag_be_verbose) printf("applying next rule\n");
    } else {
      break;
    }
  }
  if (FLAGGED == flag_be_verbose) printf("datetime parsing of string '%s', year=%04d, month=%02d, day=%02d, hour=%02d, min=%02d, sec=%02d\n", broken_datetime, year, month, day, hour, min, sec);
  /* write corrected value to new string */
  char * fixed_date = NULL;
  fixed_date=malloc(sizeof(char) * TIFFDATETIMELENGTH); /* 20 comes from TIFF definition */
  if (NULL == fixed_date) {
    fprintf(stderr, "could not allocate memory for datetime conversion, abort\n");
    exit (FIXIT_TIFF_MEMORY_ALLOCATION_ERROR);
  }
  int written = snprintf(fixed_date, TIFFDATETIMELENGTH, "%04d:%02d:%02d %02d:%02d:%02d", year, month, day, hour, min, sec);

  if (written != (TIFFDATETIMELENGTH)-1) {
    fprintf(stderr, "something wrong, instead %d chars, %d chars were written\n",TIFFDATETIMELENGTH-1 ,written);
    exit (FIXIT_TIFF_STRING_COPY_ERROR);
  }
  return fixed_date;
}

/** loads a tiff, fix it if needed, stores tiff
 * @param filename filename which should be processed, repaired
 */
void fix_tiff(const char * filename) {
  /* load file */
  TIFF* tif = TIFFOpen(filename, "r+");
  if (NULL == tif) {
    fprintf( stderr, "file '%s' could not be opened\n", filename);
    exit (FIXIT_TIFF_READ_PERMISSION_ERROR);
  };
  /* find date-tag and fix it */
  char *datetime=NULL;
  uint32 count=0;
  int found=TIFFGetField(tif, TIFFTAG_DATETIME, &datetime, &count);
  if (1==found) { /* there exists a datetime field */
    if (FLAGGED == flag_be_verbose) printf("Before correction\n-----------------\n");	
    if (FLAGGED == flag_be_verbose) TIFFPrintDirectory(tif, stdout, TIFFPRINT_NONE);
    if (FLAGGED == flag_be_verbose) printf("c=%u datetime:'%s'\n", count, datetime);
    /* should be corrected? */
    char * new_datetime = correct_datestring( datetime );
    if (0 != strncmp(datetime, new_datetime, TIFFDATETIMELENGTH)) {
      /* yes, correct TIFF DateTime is needed */
      if (FLAGGED == flag_check_only) {
        if (FLAGGED == flag_be_verbose) printf ("datetime correction needed\n");
        exit (FIXIT_TIFF_IS_CHECKED);
      } else { /* repair */
        TIFFSetField(tif, TIFFTAG_DATETIME, new_datetime);
        if (FLAGGED == flag_be_verbose) printf("After  correction\n-----------------\n");
        if (FLAGGED == flag_be_verbose) TIFFPrintDirectory(tif, stdout, TIFFPRINT_NONE);
        /* write data back, only if no flag_check_only is set */
        int written = TIFFRewriteDirectory(tif);
        if (1 != written) {
          fprintf(stderr, "something is wrong, tiffdir could not be written to file '%s'\n", filename);
          exit (FIXIT_TIFF_WRITE_ERROR);
        }
      }
    } else { /* no, should not be touched, check only */
      if (FLAGGED == flag_be_verbose) printf ("no correction needed\n");
      exit(FIXIT_TIFF_IS_VALID);
    }
  } else if (0 == found) {
    if (FLAGGED == flag_be_verbose) printf ("no datetime found!\n");
    exit(FIXIT_TIFF_IS_VALID);
  }
  TIFFClose(tif);
}

/** main */
int main (int argc, char * argv[]) {
  const char *infilename = NULL;
  const char *outfilename= NULL;
  //opterr = 0;
  int c;
  int flag_substitute_only=UNFLAGGED;
  while ((c = getopt (argc, argv, "s::cq::hi:o:")) != -1) {
      switch (c)
           {
           case 'h': /* help */
             help();
             exit (0);
           case 's': /* inplace substitution */
             flag_substitute_only = FLAGGED;
             break;
           case 'c': /* reports only if repair needed */
             flag_check_only = FLAGGED; 
             break;
           case 'q': /* disables describing messages */
             flag_be_verbose = UNFLAGGED;
             break;
           case 'i': /* expects infile */
             infilename=optarg;
             break;
           case 'o': /* expects outfile */
             outfilename=optarg;
           case '?': /* something goes wrong */
             if (optopt == 'i' || optopt == 'o')
               fprintf (stderr, "Option -%c requires an argument.\n", optopt);
             else if (isprint (optopt))
               fprintf (stderr, "Unknown option `-%c'.\n", optopt);
             else if (0 !=optopt) {
               fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
               return (-1);
             }
             break;
           default:
             abort();
           }
  }
  /* added additional checks */
  if ((FLAGGED == flag_substitute_only) && (FLAGGED == flag_check_only)) {
    fprintf (stderr, "The options '-s' and '-c' could not be used in combination, see '%s -h' for details\n", argv[0]);
    exit (FIXIT_TIFF_CMDLINE_ARGUMENTS_ERROR);
  }
  if (NULL == infilename) {
    fprintf (stderr, "You need to specify infile with '-i filename', see '%s -h' for details\n", argv[0]);
    exit (FIXIT_TIFF_MISSED_INFILE);
  }
  if ((FLAGGED!=flag_substitute_only) && (FLAGGED!=flag_check_only)) {
    if (NULL == outfilename) {
      fprintf (stderr, "You need to specify outfile with '-o outfilename', see '%s -h' for details\n", argv[0]);
      exit (FIXIT_TIFF_MISSED_OUTFILE);
    }
  }
  if (FLAGGED == flag_be_verbose) printf ("infile='%s', outfile='%s'\n", infilename, outfilename);
  if (
      (FLAGGED == flag_substitute_only) || /* inplace correction */
      (FLAGGED == flag_check_only)
     ) {
    fix_tiff(infilename);
  } else { /* source target */
    copy_file (infilename, outfilename);
    fix_tiff(outfilename);
  }
  exit (FIXIT_TIFF_IS_CORRECTED);
}

