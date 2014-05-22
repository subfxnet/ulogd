ULOGD2
======

Plugins for Linux netfilter's ulogd 2.x program.

Ulogd can be found at http://www.netfilter.org/projects/ulogd/

Filter Plugins
--------------

#### Rate Limiting

The rate limit plugin **RATE** does exactly what it says.  It allows you to throttle the amount of packets a ulogd stack will process in a given time interval.  There are two configuration options:

| **window** | The number of milliseconds in which the **limit** is counted |
| **limit**  | The number of packets to process within the given **window** |

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

In order to add the rate limit plugin to ulogd source tree, copy the `filter/ulogd_filter_RATE.c` file into the filter subdirectory of the ulogd source tree.  Next add the following to the `filter/Makefile.am` file.

```
pkglib_LTLIBRARIES += ulogd_filter_RATE.la
ulogd_filter_RATE_la_SOURCES = ulogd_filter_RATE.c
ulogd_filter_RATE_la_LDFLAGS = -avoid-version -module
```

Once `filter/Makefile.am` is updated, run the following from the ulogd source root directory:

```
$ autoreconf
$ automake
```
Then you can build the package as normal.

Output
------

#### Redis

This output plugin allows you to send your packet or flow data directly into a Redis database.  The plugin uses Redis' ability to store associative arrays (hash tables) as a value internally.  The plugin takes the key/value input from the other members of the stack and runs `HMSET` against the Redis server.

```
HMSET "keyformat" key1 value1 key2 value2 ...
```

There are several configuration options available.

* **`host`**
    * The hostname or IP address of the Redis server
    * Default value is "127.0.0.1"

* **`port`**
    * The TCP port of the Redis server
    * Default value is 6379

* **`sockpath`**
    * UNIX domain socket path to use for Redis server connection
    * No default value.  If this is set it will override host & port settings

* **`passwd`**
    * Password to use if Redis authentication is enabled
    * No default value

* **`pipeline`**
    * Number of commands to pipeline to Redis
    * Default value is 1

* **`exclude`**
    * Comma delmited list of fields to exclude from Redis persistence
    * No default value

* **`expire`**
    * Number of seconds in which Redis keys should expire
    * No default value
    * If this option is set, for every `HMSET <keyformat> ...` insert made another command `EXPIRE <keyformat> <expire>` is sent to Redis

* **`keyformat`**
    * Format to use for the Redis keys
    * Default value is "{{ip.saddr}}|{{ip.daddr}}|{{time.oob.secs}}.{{time.oob.usecs}}"

## Keyformat

The `keyformat` configuration option uses a simple macro replacement syntax to allow inserting various input fields as components of the key.  This requires some prior knowledge of the input keys the Redis output plugin will receive from the stack.  The syntax is simply `{{` keyname `}}`.  There is a maximum of 8 macros allowed in the `keyformat` configuration option.

### Building

This plugin has been tested with `hiredis 0.10.1`, but it should work with any more recent version as well.  


The additions to the `configure.ac` file will ensure the redis plugin is only built if a suitable version of the hiredis library is found on your system.
