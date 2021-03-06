/* Edit these strings to get manifest from a different URL */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <openssl/md5.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

int md5_from_file(char *hash, FILE *handle);
int create_directories(char *path);
int update_file(xmlNodePtr node);
int check_file(xmlNodePtr node);
int main(int argc, char **argv);

xmlXPathContextPtr xpath_context;

int md5_from_file(char *hash, FILE *handle) {
  MD5_CTX md5_context;
  unsigned char data_buffer[16384];
  int bytes_read;
  unsigned char digest[MD5_DIGEST_LENGTH];

  MD5_Init(&md5_context);
  while((bytes_read = fread(data_buffer, 1, 16384, handle)) != 0)
    MD5_Update(&md5_context, data_buffer, bytes_read);
  MD5_Final(digest, &md5_context);

  sprintf(hash, "%02x", digest[0]);
  for(int i = 1; i < MD5_DIGEST_LENGTH; i++)
    sprintf(hash, "%s%02x", hash, digest[i]);

  return 0;
}

int create_directories(char *path) {
  char buffer[100];
  size_t len;

  snprintf(buffer, sizeof(buffer), "%s", path);
  len = strlen(buffer);

  for(int i = len - 1; i >= 0; i--) {
    if(buffer[i] == '/')
      break;

    if(i == 0)
      return 0;

    buffer[i] = 0;
  }

  for(int i = 0; buffer[i] != 0; i++) {
    if(buffer[i] == '/') {
      buffer[i] = 0;
      mkdir(buffer, ACCESSPERMS);
      buffer[i] = '/';
    }
  }

  return 0;
}

int update_file(xmlNodePtr node) {
  CURL *curl;
  CURLcode res;
  FILE *file_handle;
  xmlXPathObjectPtr urls_object;
  xmlBufferPtr xml_buffer;
  int urls_num;
  int random_start;
  char buffer[100];
  int whitespace_offset;

  if(create_directories((char *)xmlGetProp(node, (xmlChar *)"name"))) {
    return 1;
  }

  urls_object = xmlXPathNodeEval(node, (xmlChar *)"url", xpath_context);
  urls_num = urls_object->nodesetval->nodeNr;
  random_start = rand() % urls_num;

  for(int i = random_start; i < urls_num + random_start; i++) {
    xml_buffer = xmlBufferCreate();

    xmlNodeBufGetContent(xml_buffer, urls_object->nodesetval->nodeTab[i % urls_num]);

    sprintf(buffer, "%s", xmlBufferContent(xml_buffer));

    xmlBufferFree(xml_buffer);

    for(int c = strlen(buffer) - 1; c >= 0; c--) {
      if(!isspace(buffer[c]))
        break;
      buffer[c] = 0;
    }

    for(whitespace_offset = 0; whitespace_offset < 100; whitespace_offset++) {
      if(!isspace(buffer[whitespace_offset]))
        break;
    }

    file_handle = fopen((char *)xmlGetProp(node, (xmlChar *)"name"), "wb");

    if(!file_handle) {
      return 1;
    }

    printf("Getting %s ...\n", buffer + whitespace_offset);

    curl = curl_easy_init();
    if(!curl) {
      return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, buffer + whitespace_offset);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file_handle);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    fclose(file_handle);

    if(res == CURLE_OPERATION_TIMEDOUT) {
      printf("Connection timed out. Trying another server ...\n");
      continue;
    }
    else if(res == CURLE_COULDNT_RESOLVE_HOST) {
      printf("\nCouldn't resolve host name. Trying another server ...\n");
      continue;
    }
    else if(res == CURLE_RECV_ERROR) {
      printf("Error receiving network data. Trying another server ...\n");
      continue;
    }
    else if(res == CURLE_PARTIAL_FILE) {
      printf("Partial file detected. Trying another server ...\n");
      continue;
    }
    else if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      return 1;
    }
    else if(!check_file(node))
      return 0;
    else
      printf("\nChecksum failed. Trying another server ...\n");
  }

  fprintf(stderr, "No more servers available.\nDownload failed: %s\n", buffer);
  return 1;
}

int check_file(xmlNodePtr node) {
  char buffer[100];
  char hash[100];
  FILE *file_handle;
  long file_size;

  sprintf(buffer, "%s", xmlGetProp(node, (xmlChar *)"name"));

  file_handle = fopen(buffer, "rb");

  if(!file_handle)
    return 1;

  fseek(file_handle, 0L, SEEK_END);
  file_size = ftell(file_handle);
  fclose(file_handle);

  if(file_size != atol((char *)xmlGetProp(node, (xmlChar *)"size"))) {
    return 1;
  }

  file_handle = fopen(buffer, "rb");

  md5_from_file(hash, file_handle);

  fclose(file_handle);

  if(strcmp(hash, (char *)xmlGetProp(node, (xmlChar *)"md5"))) {
    return 1;
  }

  return 0;
}

int main(int argc, char **argv) {

  if(argc < 2) {
    printf("Please provide a manifest URL as the first argument.\n");
    return 1;
  }

  char *manifest_url = argv[1];
  char *manifest_filename;

  if(argc > 2)
    manifest_filename = argv[2];
  else
    manifest_filename = "manifest.xml";

  char *manifest_new_filename;

  manifest_new_filename = malloc(strlen(manifest_filename) + 4);

  manifest_new_filename = strcpy(manifest_new_filename, manifest_filename);
  manifest_new_filename = strcat(manifest_new_filename, ".new");

  CURL *curl;
  CURLcode res;
  FILE *manifest_handle;
  xmlDocPtr manifest;
  xmlXPathObjectPtr files_object;
  xmlXPathObjectPtr apps_object;
  xmlNodePtr file_node;
  xmlBufferPtr xml_buffer;
  char buffer[100];
  char old_hash[100];
  int apps_number;
  int check_files;

  srand(time(0));

  curl = curl_easy_init();
  if(!curl) {
    return 1;
  }

  check_files = FALSE;
  manifest_handle = fopen(manifest_filename, "rb");

  if(manifest_handle) {
    md5_from_file(old_hash, manifest_handle);
    fclose(manifest_handle);
  }
  else
    check_files = TRUE;

  manifest_handle = fopen(manifest_new_filename, "wb");

  if(!manifest_handle) {
    return 1;
  }

  printf("Getting %s ...\n", manifest_url);
  curl_easy_setopt(curl, CURLOPT_URL, manifest_url);
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, TRUE);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, manifest_handle);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, FALSE);

  res = curl_easy_perform(curl);

  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    remove(manifest_new_filename);
    return 1;
  }

  curl_easy_cleanup(curl);

  fclose(manifest_handle);

  manifest_handle = fopen(manifest_new_filename, "rb");

  if(!manifest_handle) {
    check_files = TRUE;
  }
  else if(!check_files) {
    md5_from_file(buffer, manifest_handle);
    if(strcmp(old_hash, buffer))
      check_files = TRUE;
  }

  fclose(manifest_handle);

  manifest = xmlParseFile(manifest_new_filename);

  if(!manifest) {
    fprintf(stderr, "xmlReadFile() failed to parse %s\n", manifest_new_filename);
    remove(manifest_new_filename);
    return 1;
  }

  xpath_context = xmlXPathNewContext(manifest);

  if(check_files) {
    files_object = xmlXPathEvalExpression((xmlChar *)"/manifest/filelist/file", xpath_context);

    printf("Checking %d local files ...\n", files_object->nodesetval->nodeNr);

    for(int i = 1; i <= files_object->nodesetval->nodeNr; i++) {
      sprintf(buffer, "/manifest/filelist/file[%d]", i);
      file_node = xmlXPathEvalExpression((xmlChar *)buffer, xpath_context)->nodesetval->nodeTab[0];

      sprintf(buffer, "%s", xmlGetProp(file_node, (xmlChar *)"name"));

      if(buffer[0] == '/' || buffer[0] == '~' || strstr(buffer, "..")) {
        fprintf(stderr, "Path not allowed: %s\n", buffer);
        remove(manifest_new_filename);
        return 1;
      }

      if(!check_file(file_node)) {
        printf("%s is up to date.\n", buffer);
        continue;
      }

      if(update_file(file_node)) {
        remove(manifest_new_filename);
        return 1;
      }
    }

    xmlXPathFreeObject(files_object);

    rename(manifest_new_filename, manifest_filename);
  }
  else {
    printf("Manifest is up to date.\n");

    remove(manifest_new_filename);
  }

  apps_object = xmlXPathEvalExpression((xmlChar *)"/manifest/profiles/launch", xpath_context);
  apps_number = apps_object->nodesetval->nodeNr;
  printf("%d applications found.\n", apps_number);
  for(int i = 1; i <= apps_number; i++) {
    sprintf(buffer, "/manifest/profiles/launch[%d]", i);
    file_node = xmlXPathEvalExpression((xmlChar *)buffer, xpath_context)->nodesetval->nodeTab[0];

    xml_buffer = xmlBufferCreate();

    xmlNodeBufGetContent(xml_buffer, file_node);
    printf("%d: %s\n", i, xmlBufferContent(xml_buffer));

    xmlBufferFree(xml_buffer);

    printf("%s %s\n", xmlGetProp(file_node, (xmlChar *)"exec"), xmlGetProp(file_node, (xmlChar *)"params"));
  }

  xmlXPathFreeObject(apps_object);
  xmlXPathFreeContext(xpath_context);
  xmlFreeDoc(manifest);
  remove(manifest_new_filename);

  free(manifest_new_filename);
  return 0;
}
