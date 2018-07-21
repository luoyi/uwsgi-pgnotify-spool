uwsgi-pgnotify
==============

Maps PostgreSQL notification system to uWSGI spool framework

Installation
============

the plugin is 2.x friendly

```
uwsgi --buid-plugin https://github.com/luoyi/uwsgi-pgnotify-spool
```

Configuration
=============

Please see the pgnotify_spool.ini and spooler_demo.py file for config example

```ini
[uwsgi]
master          = true
plugins         = pgnotify_spool,python
socket          = /tmp/pgnotify_spool.sock
spooler         = /var/spool/pgnotify
pgnotify-spool  = TEST postgresql://uuu:ppp@127.0.0.1:5432/ddd  
uid             = http
gid             = http
spooler-import  = /tmp/spooler_demo.py
...
```

