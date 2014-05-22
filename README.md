ULOGD2
======

Plugins for Linux netfilter's ulogd 2.x program.

Ulogd can be found at http://www.netfilter.org/projects/ulogd/

Filter Plugins
--------------

### Rate Limiting

The rate limit plugin **RATE** does exactly what it says.  It allows you to throttle the amount of packets a ulogd stack will process in a given time interval.  There are two configuration options:

* **window**
    * The number of milliseconds in which the **limit** is counted

* **limit**
    * The number of packets to process within the given **window**

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

The above configuration would ensure only 100 packets are processed every 10 seconds.

Valid input plugin types for the `RATE` plugin are `ULOGD_DTYPE_SUM`, `ULOGD_DTYPE_RAW`, `ULOGD_DTYPE_PACKET` and `ULOGD_DTYPE_FLOW`

#### Building

In order to add the rate limit plugin to ulogd source tree, simply apply the `ulogd-2.0.4.filter_RATE.patch` file against the ulogd-2.0.4 source tree.  Alternatively, copy the `filter/ulogd_filter_RATE.c` file into the filter subdirectory of the ulogd source tree.  Next add the following to the `filter/Makefile.am` file.

```
pkglib_LTLIBRARIES += ulogd_filter_RATE.la
ulogd_filter_RATE_la_SOURCES = ulogd_filter_RATE.c
ulogd_filter_RATE_la_LDFLAGS = -avoid-version -module
```

Once `filter/Makefile.am` is updated, or the patch file is applied, run the following from the ulogd source root directory:

```
$ autoreconf
```
Then you can build the package as normal.

Output Plugins
--------------

### Redis

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

* **`db`**
    * The Redis database number to use
    * Default value is 1

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

##### Keyformat

The `keyformat` configuration option uses a simple macro replacement syntax to allow inserting various input fields as components of the key.  This requires some prior knowledge of the input keys the Redis output plugin will receive from the stack.  The syntax is simply `{{keyname}}`.  There is a maximum of 8 macros allowed in the `keyformat` configuration option.

#### Example configuration

Here is a sample configuration file for using the Redis output plugin.

```
plugin="/usr/lib/ulogd/ulogd_inppkt_NFLOG.so"
plugin="/usr/lib/ulogd/ulogd_raw2packet_BASE.so"
plugin="/usr/lib/ulogd/ulogd_filter_IP2STR.so"
plugin="/usr/lib/ulogd/ulogd_output_REDIS.so"

stack=log1:NFLOG,base1:BASE,ip2str:IP2STR,redis1:REDIS

[redis1]
host="127.0.0.1"
port=6379
#passwd=
#sockpath=
expire=60
exclude="src_ip, dest_ip"
pipeline=20
keyformat="{{ip.saddr}}:{{src_port}}|{{ip.daddr}}:{{dest_port}}|{{oob.time.sec}}.{{oob.time.usec}}"

[log1]
group=10

[json1]
sync=1
```

Valid input plugin types for the `REDIS` plugin are `ULOGD_DTYPE_PACKET`, `ULOGD_DTYPE_FLOW`, `ULOGD_DTYPE_SUM` and `ULOGD_DTYPE_RAW`.

#### Building

Apply the `ulogd-2.0.4.output_REDIS.patch` file to the ulogd-2.0.4 source tree.  After the patch is applied, run:

```
$ autoreconf
```

Then you can build the package normally.

This plugin has been tested with `hiredis 0.10.1`, but it should work with any more recent version as well.  

The additions to the `configure.ac` file will ensure the redis plugin is only built if a suitable version of the hiredis library is found on your system.
