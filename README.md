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

raise signal 17 whenever postgresql server on localhost notify channel FOOBAR

```ini
[uwsgi]
plugins = pgnotify
pgnotify-signal = 17 FOOBAR user=postgres
...
```

