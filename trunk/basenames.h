#ifndef H_BASENAMES
#define H_BASENAMES

/* returns a pointer inside of name */
char *ourBaseName(char *name);
/* returns a malloc'd string which must be freed by the caller */
char *ourDirName(char *origname);

#endif
