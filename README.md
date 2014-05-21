ULOGD
=====

Plugins &amp; enhancements for Linux netfilter's ulogd 2.x program.

Ulogd can be found at http://www.netfilter.org/projects/ulogd/

Filters
-------

#### Rate Limiting

The rate limit plugin **RATE** does exactly what it says.  It allows you to throttle the amount of packets a ulogd stack will process in a given time interval.  There are two configuration options:

* window   The number of milliseconds in which the **limit** is counted.
* limit    The number of packets to process within the given **window**.

A sample ulogd.conf utilizing the **RATE** plugin could look like:

```
plugin="/usr/lib/ulogd/ulogd_inppkt_NFLOG.so"
plugin="/usr/lib/ulogd/ulogd_raw2packet_BASE.so"
plugin="/usr/lib/ulogd/ulogd_filter_IP2STR.so"
plugin="/usr/lib/ulogd/ulogd_output_JSON.so"
plugin="/usr/lib/ulogd/ulogd_filter_RATE.so"

stack=log1:NFLOG,rate1:RATE,base1:BASE,ip2str:IP2STR,json1:JSON

[rate1]
window=10000
limit=100

[log1]
group=10

[json1]
sync=1
```

The above configuration would ensure only 100 packets were processed every 10 seconds.

### Building

In order to add the rate limit plugin to ulogd source tree, copy the `ulogd_filter_RATE.c` file into the filter subdirectory of the expanding ulogd source tree.  Next add the following to the ulogd/filter/Makefile.am file.

```
pkglib_LTLIBRARIES += ulogd_filter_RATE.la
ulogd_filter_RATE_la_SOURCES = ulogd_filter_RATE.c
ulogd_filter_RATE_la_LDFLAGS = -avoid-version -module
```
  
Output
------

#### Redis

This output plugin allows you to send your packet or flow data directly into a Redis database.  While this is self explanatory, there are several configuration options available.

* host
* port
* sockpath
* passwd
* pipeline
* exclude
* expire
* keyformat

