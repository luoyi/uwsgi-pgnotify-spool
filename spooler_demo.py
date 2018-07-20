import uwsgi
import json


def convert(data):
  if isinstance(data, bytes):      return data.decode()
  if isinstance(data, (str, int)): return str(data)
  if isinstance(data, dict):       return dict(map(convert, data.items()))
  if isinstance(data, tuple):      return tuple(map(convert, data))
  if isinstance(data, list):       return list(map(convert, data))
  if isinstance(data, set):        return set(map(convert, data))

def my_spooler(kwargs):
    print(convert(kwargs))
    return uwsgi.SPOOL_OK



uwsgi.spooler = my_spooler
