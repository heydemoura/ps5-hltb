/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hltb.h"
#include <stdio.h>
int main(int argc,char**argv){
 const char*title=argc>1?argv[1]:"Elden Ring";
 printf("Querying HowLongToBeat by title: %s\n",title);fflush(stdout);
 hltb_result r={0};int status=hltb_lookup(title,&r);
 if(status){printf("Lookup failed: %d (1=no title match, 2=no Main Story data)\n",status);return 1;}
 char label[100];if(hltb_format_time(r.main_seconds,label,sizeof label))return 1;
 printf("HLTB game=%d title=%s seconds=%u\n%s\n",r.game_id,r.game_name,r.main_seconds,label);
 return 0;
}
