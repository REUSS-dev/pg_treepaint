# pg_treepaint
A helper extension to collect query parsing, planning & execution data and send it to a [TreePaint Client](https://github.com/REUSS-dev/pg_treepaint_client) via a TCP Socket.

# Building & installation
1. Go to `contrib` folder of your copy of PostgreSQL source code.
2. Clone this repository into `contrib` folder: `git clone https://github.com/REUSS-dev/pg_treepaint`
3. `cd` into *pg_treepaint*
4. `make && make install`

# Usage
1. Load extension using `LOAD 'pg_treepaint';`
2. Optionally add `pg_treepaint` to your *postgresql.conf*'s `shared_preload_libraries` so it loads automatically.
3. Use EXPLAIN option `tp_plan` to send collected query data to a TreePaint Client.\
As in: `EXPLAIN (tp_plan) SELECT * FROM pg_extension;`

Please note that current version of an extension is not ready for production use and EXPLAIN data payload is unencrypted at the moment.\
Use only on localhost and/or in controlled environments. 

# Configuration
For your plan data to reach TreePaint Client, you must supply extension with a valid IP and port that TreePaint Client listens at.

There are 3 possible sources for ip and port of endpoint TreePaint Client and 3 levels of priority respectively.

| Priority | Source | IP parameter name | Port parameter name | Comment |
| -------- | ------ | ----------------- | ------------------- | ------- |
| 1        | EXPLAIN options | tp_ip | tp_port | Example: `EXPLAIN (tp_plan, tp_ip '127.0.0.1', tp_port 1111)`
| 2        | GUC parameters  | pg_treepaint.ip | pg_treepaint.port | Define your IP and port in postgresql.conf using these parameters to avoid specifying tp_ip & tp_port every time. |
| 3        | Hardcoded defaults | 127.0.0.1 | 4523 | These are values hardcoded in pg_treepaint.c and used by default if no ip or port defined elsewhere. |

> [!NOTE]
> If you have [TreePaint Client](https://github.com/REUSS-dev/pg_treepaint_client) open, you will see IP:PORT pair that it listens to in top-right corner.
> <p align="center"> <img width="184" height="59" alt="image" src="https://github.com/user-attachments/assets/9ac47e05-4f73-4e5d-89ee-6a9056cf5680" /> </p>
> If you are seeing "TCP Inactive" instead, click the TCP Applet to enable TCP Client.
>
> <i>You can set TCP client to auto-start in TreePaint Client's conf.lua file by changing CLIENT_AUTOSTART from false to true.</i>