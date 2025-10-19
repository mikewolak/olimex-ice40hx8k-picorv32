/*
 * This file is a part of minicom.
 *
 * Convert from ja_JP.ujis (EUC-Japan) to ja_JP.sjis (Shift-JIS).
 * with duplicating backslash and percent, which may appear
 * in the 2nd byte of Shift-JIS characters.
 *
 * by Tomohiro KUBOTA <kubota@debian.or.jp>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned short ujis2sjis(unsigned char ku, unsigned char ten)
{
	unsigned char s1,s2;
	s1 = (ku-1)/2 + 129;
	if (ku >= 63) s1 += 193-129;
	if (!(ku&1)) s2 = ten+158;
	else if (ten <= 63) s2 = ten+63;
	else s2 = ten+64;
	return s1*256+s2;
}

int decode_line(unsigned char buf[],int format)
{
	unsigned char kanji=0, *p=buf,c1,c2;
	unsigned short c;
	while(1){
		if (!*p) break;
		if (kanji) {
			c=ujis2sjis((kanji&127)-32,(*p&127)-32);
			c1=c>>8; c2=c&255;
			putchar(c1); putchar(c2);
			if (c2=='\\') putchar('\\');
			if (c2=='%' && format) putchar('%');
			kanji=0;
		} else {
			if (*p&0x80) kanji=*p; else putchar(*p);
		}
		p++;
	}
	return 0;
}

main()
{
	unsigned char buf[256];
	int format=0;
	while(1){
		if (fgets(buf,255,stdin)==NULL) break;
		if (buf[0]=='\n' || buf[0]==0) format=0;
		if (buf[0]=='#' && strstr(buf,"c-format")) format=1;
		decode_line(buf,format);
	}
	exit(0);
}

