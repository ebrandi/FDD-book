/*
 * driver_skeleton.c - DELIBERATELY MESSY starter file.
 *
 * Companion for "Hands-On Lab 4: Putting It All Together" in the
 * "Good Practices for C Programming" section of Chapter 4.
 *
 * The file below compiles, but it is full of problems the chapter
 * asks you to find and fix:
 *
 *   - inconsistent indentation and brace placement
 *   - cryptic variable names (D, nm, ret)
 *   - misleading comments
 *   - macro with side effects (DOUB)
 *   - buffer overflow risk (strcpy into a 5-byte buffer)
 *   - off-by-one loop (<=)
 *   - assignment in an if condition (if (ret = 10))
 *   - missing argument checks in main()
 *   - no const on inputs that are never modified
 *
 * Your job is to rewrite this in FreeBSD KNF style, with safe
 * string handling, proper error checking, and small helper
 * functions. A short list of tasks appears in the chapter under
 * "Hands-On Lab 4: Putting It All Together".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DOUB(X) X+X

/* this function tries to init device, but its messy */
int initDev(int D, char *nm){
int i=0;char buf[5];int ret;
if(D==0){printf("bad dev\n");return -1;}
strcpy(buf,nm);
for(i=0;i<=D;i++){ret=DOUB(i);}
/* check if ret bigger then 10 */
if(ret=10){printf("ok\n");}
else{printf("fail\n");}
return ret;}

/* main func */
int main(int argc,char **argv){int dev;char*name;
dev=atoi(argv[1]);name=argv[2];
int r=initDev(dev,name);
printf("r=%d\n",r);}
