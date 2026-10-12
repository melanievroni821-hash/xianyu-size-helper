#include "video_core.h"
int main(int argc,char **argv){if(argc!=5)return 2;char out[8192];if(!filter_graph(out,sizeof(out),atoi(argv[1]),atoi(argv[2]),atoi(argv[3]),atoi(argv[4])))return 1;puts(out);return 0;}
