"""The language of the data scripts' messages (assets/*/fetch.py, tools/dag_veri.py, tools/olcek_veri.py).

They follow the application: lang in user.cfg at the repository root, which the gallery's language button writes and
src/app/lang.cpp reads the same way. --lang tr|en on the command line overrides it. With neither (the scripts are often
run before the application has ever been opened) a message is given in both, English first.

  import dil
  dil.choose(args.lang)            # or dil.choose(dil.take_lang(sys.argv)) in a script without argparse
  print(dil.say('Türkçe', 'English'))
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANG = None   # 'tr', 'en', or None for both


def app_lang():
    """the language the application was last left in, or None when it has not saved one"""
    try:
        with open(os.path.join(ROOT, 'user.cfg'), encoding='utf-8') as f:
            for line in f:
                p = line.split()
                if len(p) >= 3 and p[0] == 'lang' and p[1] == '=':
                    return 'en' if p[2] == 'en' else 'tr'
    except OSError:
        pass
    return None


def argv_lang(argv=None):
    """--lang tr|en (or --lang=tr) from the command line without consuming it (argparse still sees it), else None"""
    argv = sys.argv if argv is None else argv
    for i, a in enumerate(argv):
        if a == '--lang' and i + 1 < len(argv):
            return argv[i + 1] if argv[i + 1] in ('tr', 'en') else None
        if a.startswith('--lang='):
            v = a.split('=', 1)[1]
            return v if v in ('tr', 'en') else None
    return None


def take_lang(argv):
    """argv_lang, and the option taken out of argv (for scripts that read sys.argv by position)"""
    lang = argv_lang(argv)
    for i, a in enumerate(argv):
        if a.startswith('--lang='):
            del argv[i]
            break
        if a == '--lang':
            del argv[i:i + 2]
            break
    return lang


def choose(lang=None):
    """sets the language (the given one, else the application's) and makes sure Turkish letters can be printed"""
    global LANG
    LANG = lang or app_lang()
    for s in (sys.stdout, sys.stderr):
        try:
            'ığşİçöü'.encode(s.encoding or 'ascii')
        except (UnicodeEncodeError, LookupError):
            try:
                s.reconfigure(encoding='utf-8')
            except (AttributeError, ValueError):
                pass
    return LANG


def say(tr, en):
    """a message in the chosen language, or both (English first) when none is known"""
    return tr if LANG == 'tr' else en if LANG == 'en' else en + '\n' + tr
