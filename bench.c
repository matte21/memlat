//#include <asm/msr.h>
#include <stdio.h>
#include <stdlib.h>

#define rdtsc(low,high) \
     __asm__ __volatile__("mfence" ::: "memory"); \
     __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high)); \
     __asm__ __volatile__("mfence" ::: "memory");



typedef unsigned long long u64;

static inline u64 rdtsc_read(void)
{
	unsigned long high, low;
	rdtsc(low,high);
	return ((u64)high << 32) | low;
}

int* hist;
int histsize;

int main(int argc, char* argv[])
{
  int maxhist = 10000;
  int bufsize = 1000000;
  int stride  = 984567;
  int maxaccs = 50;
  int rounds, cycle, bufentries;
  void **buf, **pos;
  int i, inext, j, accs = 0;
  u64 diff, start, maxdiff;
  int istart, iend, res = 0;

  if (argc >1) bufsize = atoi(argv[1]) * 1000;
  if (argc >2) maxaccs = atoi(argv[2]);
  if (argc >3) stride = atoi(argv[3]);

  if ((bufsize == 0) || (maxaccs == 0) || (stride == 0)) {
     fprintf(stderr,
	     "Memlat: Histogram of access latencies from linked-list traversal.\n"
	     "Usage: %s [<size> [<accs> [<stride>]]]\n\n"
	     " <size>    Buffer size in K (def. 1000 for 1MB)\n"
	     " <accs>    Maximal number of accesses done in Mio. (def. 50)\n"
	     " <stride>  Stride between accesses (def. huge for pseudo-random)\n", argv[0]);
     exit(1);
  }
  
  bufentries = bufsize / sizeof(void*);
  buf = (void**) malloc( bufsize );
  for(i = 0;i<bufentries;i++)
    buf[i] = buf;

  fprintf(stderr, "Buf at %p (size %d, entries %d), Stride %d.\n",
	 buf, bufsize, bufentries, stride);
  
  cycle = 0;
  i = 0;
  do {
    inext = i+stride;
    while(inext>=bufentries) inext -= bufentries;
    buf[i] = buf + inext;
    cycle++;
    i = inext;
  } while(i != 0);

  fprintf(stderr,"=> CycleLen %d\n", cycle);

  maxdiff = 0;
  for(i=0;i<2;i++) {
    pos = buf;
    for(j=0;j<cycle;j++) {
      start = rdtsc_read();
      pos = *pos;
      diff = rdtsc_read() - start;
      if (diff>maxhist) {
        fprintf(stderr, "xxxxxxxxx found diff bigger than maxhist!\n");
        return 1;
      }
      if (maxdiff < diff) maxdiff = diff;
    }
  }

  res += (int)(pos-buf);

  fprintf(stderr, "Max. CPU Cycles: %llu\n", maxdiff);

  histsize = maxdiff + 100;
  hist = (int*) malloc( histsize * sizeof(int) );
  for(i = 0;i<histsize;i++)
    hist[i] = 0;

  rounds = (int)(1000000.0 * maxaccs / cycle + 0.5);

  fprintf(stderr, "Running %d Iterations...\n",rounds);
  for(i=0;i<rounds;i++) {
    pos = buf;
    for(j=0;j<cycle;j++) {
      start = rdtsc_read();
      pos = *pos;
      diff = rdtsc_read() - start;
      if (diff>histsize) {
        fprintf(stderr, "xxxxxxxxx found diff bigger than histsize!\n");
        return 1;
      }
      hist[diff]++;
      accs++;
    }
  }

  res += (int)(pos-buf);

  fprintf(stderr, "Done %d accesses...\n", accs);

  double ovd = 0;
  for(j=0;j<cycle;j++) {
    start = rdtsc_read();
    diff = rdtsc_read() - start;
    ovd += ((double)(diff - ovd))/((double)(j+1));
  }
  u64 avg_ov = (u64) ovd;
  fprintf(stderr, "Avg measurement overhead per access: %llu cycles\n", avg_ov);

#if 0
  istart=0;
  iend = histsize-1;
#else
  for(istart = 0;istart<histsize;istart++)
    if (hist[istart] > accs/1000) break;
  for(iend = histsize-1;iend>=0;iend--)
    if (hist[iend] > accs/1000) break;
#endif

  //if (istart<ov) istart = ov;
  //iend += 20;
  //if (iend>=histsize) iend = histsize-1;

  fprintf(stderr, "Dumping histogram [%llu;%llu] (res %d)\n", istart-avg_ov, iend-avg_ov, res);


  for(i = istart;i<iend;i++)
    printf("%llu %.2f\n", i-avg_ov, 1000.0 * hist[i] / accs );
}
