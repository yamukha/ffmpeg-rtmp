#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

long get_time_ms (void)
{
    long            ms; // Milliseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = s * 1000 + round(spec.tv_nsec / 1.0e6); // Convert nanoseconds to milliseconds

    return ms;
}

int kbhit(void)
{
  struct termios oldt, newt;
  int ch;
  int oldf;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  ch = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  if(ch != EOF &&  ' '== ch )
  {
     return 1;
  }

  return 0;
}
