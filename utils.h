#ifndef UTILS_H
#define UTILS_H

#define min(X,Y) (((X) < (Y)) ? (X) : (Y))
#define max(X,Y) (((X) > (Y)) ? (X) : (Y))
long get_time_ms (void);
int kbhit(void);
void print_usage();

#endif
