#include "char.h"

/*
 * _ctype - classifies each ASCII character as space (_S),
 * upper-case letter (_U), lower-case letter (_L), or
 * digit (_N), or other alphabetic (_A, e.g. "_")
 */

char _ctype[] = {
    0,   0,   0,   0,   0,   0,   0,   0,       /* NUL-BEL  */
    0,  _S,  _S,  _S,  _S,  _S,   0,   0,       /* BS -SI   */
    0,   0,   0,   0,   0,   0,   0,   0,       /* DLE-ETB  */
    0,   0,   0,   0,   0,   0,   0,   0,       /* CAN-US   */
   _S,   0,   0,   0,   0,   0,   0,   0,       /*  !"#$%&' */
    0,   0,   0,   0,   0,   0,   0,   0,       /* ()*+,-./ */
   _N,  _N,  _N,  _N,  _N,  _N,  _N,  _N,       /* 01234567 */
   _N,  _N,   0,   0,   0,   0,   0,   0,       /* 89:;<=>? */
    0,  _U,  _U,  _U,  _U,  _U,  _U,  _U,       /* @ABCDEFG */
   _U,  _U,  _U,  _U,  _U,  _U,  _U,  _U,       /* HIJKLMNO */
   _U,  _U,  _U,  _U,  _U,  _U,  _U,  _U,       /* PQRSTUVW */
   _U,  _U,  _U,   0,   0,   0,   0,  _A,       /* XYZ[\]^_ */
    0,  _L,  _L,  _L,  _L,  _L,  _L,  _L,       /* `abcdefg */
   _L,  _L,  _L,  _L,  _L,  _L,  _L,  _L,       /* hijklmno */
   _L,  _L,  _L,  _L,  _L,  _L,  _L,  _L,       /* pqrstuvw */
   _L,  _L,  _L,   0,   0,   0,   0,   0        /* xyz{|}~  */
   };

/*
 * ctran - translation table for all input except in string literals
 */

char ctran[] = {
   000, 001, 002, 003, 004, 005, 006, 007,      /* NUL-BEL  */
   010, 011, 012, 013, 014, 015, 016, 017,      /* BS -SI   */
   020, 021, 022, 023, 024, 025, 026, 027,      /* DLE-ETB  */
   030, 031, 032, 033, 034, 035, 036, 037,      /* CAN-US   */
   ' ', '!', '"', '#', '$', '%', '&','\'',      /*  !"#$%&' */
   '(', ')', '*', '+', ',', '-', '.', '/',      /* ()*+,-./ */
   '0', '1', '2', '3', '4', '5', '6', '7',      /* 01234567 */
   '8', '9', ':', ';', '<', '=', '>', '?',      /* 89:;<=>? */
   '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',      /* @ABCDEFG */
   'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',      /* HIJKLMNO */
   'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',      /* PQRSTUVW */
   'X', 'Y', 'Z', '[','\\', ']', '^', '_',      /* XYZ[\]^_ */
   '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g',      /* `abcdefg */
   'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',      /* hijklmno */
   'p', 'q', 'r', 's', 't', 'u', 'v', 'w',      /* pqrstuvw */
   'x', 'y', 'z', '{', '|', '}', '~',0177,      /* xyz{|}~  */
   };

/*
 * esctab - translates single-character escapes in string literals
 */

char esctab[] = {
   000, 001, 002, 003, 004, 005, 006, 007,      /* NUL-BEL  */
   010, 011, 012, 013, 014, 015, 016, 017,      /* BS -SI   */
   020, 021, 022, 023, 024, 025, 026, 027,      /* DLE-ETB  */
   030, 031, 032, 033, 034, 035, 036, 037,      /* CAN-US   */
   ' ', '!', '"', '#', '$', '%', '&','\'',      /*  !"#$%&' */
   '(', ')', '*', '+', ',', '-', '.', '/',      /* ()*+,-./ */
   000, 001, 002, 003, 004, 005, 006, 007,      /* 01234567 */
   010, 011, ':', ';', '<', '=', '>', '?',      /* 89:;<=>? */
   '@', 'A','\b', 'C',0177, 033, 014, 'G',      /* @ABCDEFG */
   'H', 'I', 'J', 'K','\n', 'M','\n', 'O',      /* HIJKLMNO */
   'P', 'Q','\r', 'S','\t', 'U', 013, 'W',      /* PQRSTUVW */
   'X', 'Y', 'Z', '[','\\', ']', '^', '_',      /* XYZ[\]^_ */
   '`', 'a','\b', 'c',0177, 033, 014, 'g',      /* `abcdefg */
   'h', 'i', 'j', 'k','\n', 'm','\n', 'o',      /* hijklmno */
   'p', 'q','\r', 's','\t', 'u', 013, 'w',      /* pqrstuvw */
   'x', 'y', 'z', '{', '|', '}', '~',0177,      /* xyz{|}~  */
   };
