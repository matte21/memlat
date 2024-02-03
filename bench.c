//#include <asm/msr.h>
#include <stdio.h>
#include <stdlib.h>

#define rdtsc(low,high) \
     __asm__ __volatile__("cpuid" : : "a" (0) : "bx", "cx", "dx"); \
     __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high)); \
     __asm__ __volatile__("cpuid" : : "a" (0) : "bx", "cx", "dx");

typedef unsigned long long u64;

static inline u64 rdtsc_read(void)
{
	unsigned long high, low;
	rdtsc(low,high);
	return ((u64)high << 32) | low;
}

int* hist;

typedef struct {
  long long int p50, p90, p95, p99;
} percentiles;

// compute_percentiles assumes that all the computed percentiles do exist (i.e. it fails silently
// if the input parameters are not consistent between them).
percentiles compute_percentiles(int *hist, int overhead, int histsize, int accs) {
  percentiles p = {-1000000, -1000000, -1000000, -1000000};
  u64 accs_seen_so_far = 0;
  for (int i = 0; i < histsize; i++) {
    accs_seen_so_far += hist[i];
    if (p.p50 == -1000000 && accs_seen_so_far >= (accs * 0.5)) {
      p.p50 = i - overhead;
    }
    if (p.p90 == -1000000 && accs_seen_so_far >= (accs * 0.9)) {
      p.p90 = i - overhead;
    }
    if (p.p95 == -1000000 && accs_seen_so_far >= (accs * 0.95)) {
      p.p95 = i - overhead;
    }
    if (p.p99 == -1000000 && accs_seen_so_far >= (accs * 0.99)) {
      p.p99 = i - overhead;
    }
  }
  return p;
}

int main(int argc, char* argv[])
{
  int bufsize = 1000000;
  int stride  = 984567;
  int histsize = 400;
  int maxaccs = 50;
  int rounds, cycle, bufentries;
  void **buf, **pos;
  int i, inext, j, accs = 0;
  u64 diff, start;

  if (argc >1) bufsize = atoi(argv[1]) * 1000;
  if (argc >2) maxaccs = atoi(argv[2]);
  if (argc >3) stride = atoi(argv[3]);
  if (argc >4) histsize = atoi(argv[4]);

  if ((bufsize == 0) || (maxaccs == 0) || (stride == 0)) {
     fprintf(stderr,
	     "Memlat: Histogram of access latencies from linked-list traversal.\n"
	     "Usage: %s [<size> [<accs> [<stride>]]]\n\n"
	     " <size>     Buffer size in K (def. 1000 for 1MB)\n"
	     " <accs>     Maximal number of accesses done in Mio. (def. 50)\n"
	     " <stride>   Stride between accesses (def. huge for pseudo-random)\n"
       " <histsize> Maximum number of Cycles that a memory access can have (def. 400)\n", argv[0]);
     exit(1);
  }

  bufentries = bufsize / sizeof(void*);
  buf = (void**) malloc( bufsize );
  for(i = 0;i<bufentries;i++)
    buf[i] = buf;

  fprintf(stderr, "Buf at %p (size %d, entries %d), Stride %d.\n", buf, bufsize, bufentries, stride);

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

  free(buf);

  fprintf(stderr, "Done %d accesses...\n", accs);

  double avg_ovd = 0;
  double max_ov = 0;
  double min_ov = 10000000;
  for(j=0;j<cycle;j++) {
    start = rdtsc_read();
    diff = rdtsc_read() - start;
    avg_ovd += ((double)(diff - avg_ovd))/((double)(j+1));
    if (diff > max_ov) {
      max_ov = diff;
    }
    if (diff < min_ov) {
      min_ov = diff;
    }
  }
  u64 avg_ov = (u64) avg_ovd;
  fprintf(stderr, "measurement overhead per access: avg: %llu, max: %llu, min: %llu\n", avg_ov, max_ov, min_ov);

  u64 below_ov_accs = 0;
  for(i = 0; i<avg_ov; i++)
    below_ov_accs += hist[i];
  fprintf(stderr, "Number of Accesses below the overhead: %llu (%.2f %%)\n", below_ov_accs, ((1000.0 * below_ov_accs)/accs)/10);

  fprintf(stderr, "Dumping histogram [%llu;%llu]\n", -avg_ov, histsize-avg_ov);

  percentiles p = compute_percentiles(hist, avg_ov, histsize, accs);

  for (i = 0; i<histsize; i++)
    printf("%llu %.2f\n", i-avg_ov, (1000.0 * hist[i] / (double) accs) / (double) 10);

  free(hist);

  return 0;
}
