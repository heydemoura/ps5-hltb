/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "catalog.h"
#include "cJSON.h"
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char*read_file(const char*path,size_t*size){
 FILE*f=fopen(path,"rb");if(!f)return NULL;
 if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long len=ftell(f);
 if(len<0||len>1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 unsigned char*b=malloc((size_t)len+1);if(!b){fclose(f);return NULL;}
 if(fread(b,1,len,f)!=(size_t)len){free(b);fclose(f);return NULL;}
 fclose(f);b[len]=0;*size=len;return b;
}
static uint32_t le32(const unsigned char*b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static uint16_t le16(const unsigned char*b){return (uint16_t)b[0]|(uint16_t)b[1]<<8;}
/* Parse PS4 SFO with bounds checks; never write console metadata. */
static int sfo_title(const unsigned char*b,size_t len,char*out,size_t cap){
 if(len<20||memcmp(b,"\0PSF",4))return -1;
 size_t keys=le32(b+8),values=le32(b+12),count=le32(b+16);
 if(count>(len-20)/16||keys>len||values>len)return -1;
 for(size_t i=0;i<count;i++){
  const unsigned char*e=b+20+16*i;size_t ko=le16(e),vo=le32(e+12),n=le32(e+4);
  if(ko>=len-keys||!memchr(b+keys+ko,0,len-keys-ko))continue;
  if(strcmp((const char*)b+keys+ko,"TITLE"))continue;
  if(vo>len-values||!n||n>len-values-vo||n>cap||!memchr(b+values+vo,0,n))return -1;
  memcpy(out,b+values+vo,n);return 0;
 }return -1;
}
size_t hltb_catalog_load(struct hltb_game*games,size_t cap){
 const char*roots[]={"/user/appmeta","/system_data/priv/appmeta"};size_t count=0;
 for(size_t r=0;r<sizeof roots/sizeof roots[0];r++){
  DIR*d=opendir(roots[r]);if(!d)continue;struct dirent*e;
  while(count<cap&&(e=readdir(d))){
   if(strlen(e->d_name)!=9||(strncmp(e->d_name,"PPSA",4)&&strncmp(e->d_name,"CUSA",4)))continue;
   size_t i;for(i=0;i<count;i++)if(!strcmp(games[i].id,e->d_name))break;if(i<count)continue;
   char path[256],title[512]={0};size_t len=0;
   snprintf(path,sizeof path,"%s/%.9s/param.json",roots[r],e->d_name);
   unsigned char*b=read_file(path,&len);
   if(b){cJSON*json=cJSON_Parse((char*)b);free(b);cJSON*params=cJSON_GetObjectItemCaseSensitive(json,"localizedParameters");cJSON*en=cJSON_GetObjectItemCaseSensitive(params,"en-US");cJSON*t=cJSON_GetObjectItemCaseSensitive(en,"titleName");if(cJSON_IsString(t)&&strlen(t->valuestring)<sizeof title)strcpy(title,t->valuestring);cJSON_Delete(json);}
   if(!*title){snprintf(path,sizeof path,"%s/%.9s/param.sfo",roots[r],e->d_name);b=read_file(path,&len);if(b){sfo_title(b,len,title,sizeof title);free(b);}}
   if(*title){strcpy(games[count].id,e->d_name);strcpy(games[count].title,title);count++;}
  }closedir(d);
 }return count;
}
