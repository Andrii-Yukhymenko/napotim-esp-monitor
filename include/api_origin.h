#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline bool validApiSourceChange(const char* oldOrigin,const char* newOrigin,const char* oldKey,const char* newKey) {
  return strcmp(oldOrigin,newOrigin)==0 || (*newKey && strcmp(oldKey,newKey)!=0);
}

// HTTP is supported only for literal private IPv4 development addresses.
// HTTPS permits a DNS name / IPv4 and an optional validated port, no path/userinfo.
inline bool validApiOrigin(const char* value) {
  bool https=strncmp(value,"https://",8)==0;
  bool http=strncmp(value,"http://",7)==0;
  if ((!http && !https) || strlen(value)>180) return false;
  const char* host=value+(https?8:7);
  if (!*host) return false;
  unsigned hostLength=0;const char* cursor=host;
  while (*cursor && *cursor!=':') {
    char ch=*cursor++;
    if (!((ch>='a'&&ch<='z') || (ch>='A'&&ch<='Z') || (ch>='0'&&ch<='9') || ch=='.' || ch=='-')) return false;
    ++hostLength;
  }
  if (!hostLength || host[0]=='.' || host[0]=='-' || host[hostLength-1]=='.' || host[hostLength-1]=='-') return false;
  if (*cursor==':') {
    ++cursor;if (!*cursor) return false;uint32_t port=0;unsigned digits=0;
    while (*cursor) {
      if (*cursor<'0'||*cursor>'9'||++digits>5) return false;
      port=port*10+(*cursor++-'0');
    }
    if (!port || port>65535) return false;
  }
  if (https) return true;
  uint16_t octets[4]={0};unsigned part=0,digits=0;
  for (unsigned i=0;i<hostLength;++i) {
    char ch=host[i];
    if (ch=='.') { if (!digits || part>=3) return false;++part;digits=0; }
    else {
      // Avoid inet_aton octal interpretation turning 010.* into public 8.*.
      if (!digits && ch=='0' && i+1<hostLength && host[i+1]!='.') return false;
      if (ch<'0'||ch>'9'||++digits>3) return false;
      octets[part]=octets[part]*10+(ch-'0');if (octets[part]>255) return false;
    }
  }
  if (part!=3 || !digits) return false;
  return octets[0]==10 || (octets[0]==192 && octets[1]==168) ||
    (octets[0]==172 && octets[1]>=16 && octets[1]<=31) ||
    (octets[0]==127 && octets[1]==0 && octets[2]==0 && octets[3]==1);
}
