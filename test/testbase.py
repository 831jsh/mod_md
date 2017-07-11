# test mod_md acme terms-of-service handling

import os
import shutil
import subprocess
import re
import pytest
import sys
import time
import json
import OpenSSL

from datetime import datetime
from datetime import tzinfo
from datetime import timedelta
from ConfigParser import SafeConfigParser
from httplib import HTTPConnection
from shutil import copyfile
from urlparse import urlparse


class TestEnv:

    @classmethod
    def init( cls ) :
        cls.config = SafeConfigParser()
        cls.config.read('test.ini')
        cls.PREFIX = cls.config.get('global', 'prefix')

        cls.ACME_URL_DEFAULT  = cls.config.get('acme', 'url_default')
        cls.ACME_URL  = cls.config.get('acme', 'url')
        cls.ACME_TOS  = cls.config.get('acme', 'tos')
        cls.ACME_TOS2 = cls.config.get('acme', 'tos2')
        cls.WEBROOT   = cls.config.get('global', 'server_dir')
        cls.TESTROOT   = os.path.join(cls.WEBROOT, '..', '..')

        cls.APACHECTL = os.path.join(cls.PREFIX, 'bin', 'apachectl')
        cls.ERROR_LOG = os.path.join(cls.WEBROOT, "logs", "error_log")
        cls.APACHE_CONF_DIR = os.path.join(cls.WEBROOT, "conf")
        cls.APACHE_SSL_DIR = os.path.join(cls.APACHE_CONF_DIR, "ssl")
        cls.APACHE_TEST_CONF = os.path.join(cls.APACHE_CONF_DIR, "test.conf")
        cls.APACHE_CONF_SRC = "data"

        cls.HTTP_PORT = cls.config.get('global', 'http_port')
        cls.HTTPS_PORT = cls.config.get('global', 'https_port')
        cls.HTTPD_HOST = "localhost"
        cls.HTTPD_URL = "http://" + cls.HTTPD_HOST + ":" + cls.HTTP_PORT

        cls.A2MD      = cls.config.get('global', 'a2md_bin')

        cls.MD_S_UNKNOWN = 0
        cls.MD_S_INCOMPLETE = 1
        cls.MD_S_COMPLETE = 2
        cls.MD_S_EXPIRED = 3
        cls.MD_S_ERROR = 4

        cls.set_store_dir('md')
        cls.EMPTY_JOUT = { 'status' : 0, 'output' : [] }

        cls.ACME_SERVER_DOWN = False
        cls.ACME_SERVER_OK = False


    @classmethod
    def set_store_dir( cls, dir ) :
        cls.STORE_DIR = os.path.join(cls.WEBROOT, dir)
        cls.a2md_stdargs([cls.A2MD, "-a", cls.ACME_URL, "-d", cls.STORE_DIR, "-j" ])
        cls.a2md_rawargs([cls.A2MD, "-a", cls.ACME_URL, "-d", cls.STORE_DIR ])
        
        
    # --------- cmd execution ---------

    _a2md_args = []
    _a2md_args_raw = []
    
    @classmethod
    def run( cls, args ) :
        print "execute: ", " ".join(args)
        p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (output, errput) = p.communicate()
        rv = p.wait()
        print "stderr: ", errput
        try:
            jout = json.loads(output)
        except:
            jout = None
            print "stdout: ", output
        return { 
            "rv": rv, 
            "stdout": output, 
            "stderr": errput,
            "jout" : jout 
        }

    @classmethod
    def a2md_stdargs( cls, args ) :
        cls._a2md_args = [] + args 

    @classmethod
    def a2md_rawargs( cls, args ) :
        cls._a2md_args_raw = [] + args
         
    @classmethod
    def a2md( cls, args, raw=False ) :
        preargs = cls._a2md_args
        if raw :
            preargs = cls._a2md_args_raw
        return cls.run( preargs + args )

    # --------- HTTP ---------

    @classmethod
    def is_live( cls, url, timeout ) :
        server = urlparse(url)
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                c = HTTPConnection(server.hostname, server.port, timeout=timeout)
                c.request('HEAD', server.path)
                resp = c.getresponse()
                c.close()
                return True
            except IOError:
                print "connect error:", sys.exc_info()[0]
                time.sleep(.2)
            except:
                print "Unexpected error:", sys.exc_info()[0]
                time.sleep(.2)
        print "Unable to contact server after %d sec" % timeout
        return False

    @classmethod
    def is_dead( cls, url, timeout ) :
        server = urlparse(url)
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                c = HTTPConnection(server.hostname, server.port, timeout=timeout)
                c.request('HEAD', server.path)
                resp = c.getresponse()
                c.close()
                time.sleep(.2)
            except IOError:
                return True
            except:
                return True
        print "Server still responding after %d sec" % timeout
        return False

    @classmethod
    def get_json( cls, url, timeout ) :
        data = cls.get_plain( url, timeout )
        if data:
            return json.loads(data)
        return None

    @classmethod
    def get_plain( cls, url, timeout ) :
        server = urlparse(url)
        try_until = time.time() + timeout
        while time.time() < try_until:
            try:
                c = HTTPConnection(server.hostname, server.port, timeout=timeout)
                c.request('GET', server.path)
                resp = c.getresponse()
                data = resp.read()
                c.close()
                return data
            except IOError:
                print "connect error:", sys.exc_info()[0]
                time.sleep(.1)
            except:
                print "Unexpected error:", sys.exc_info()[0]
        print "Unable to contact server after %d sec" % timeout
        return None

    @classmethod
    def check_acme( cls ) :
        if cls.ACME_SERVER_OK:
            return True
        if cls.ACME_SERVER_DOWN:
            pytest.skip(msg="ACME server not running")
            return False
        if cls.is_live(cls.ACME_URL, 0.5):
            cls.ACME_SERVER_OK = True
            return True
        else:
            cls.ACME_SERVER_DOWN = True
            pytest.fail(msg="ACME server not running", pytrace=False)
            return False


    # --------- access local store ---------

    @classmethod
    def clear_store( cls ) : 
        print("clear store dir: %s" % TestEnv.STORE_DIR)
        assert len(TestEnv.STORE_DIR) > 1
        if os.path.exists(TestEnv.STORE_DIR):
            shutil.rmtree(TestEnv.STORE_DIR, ignore_errors=False)
        os.makedirs(TestEnv.STORE_DIR)

    @classmethod
    def path_account( cls, acct ) : 
        return os.path.join(TestEnv.STORE_DIR, 'accounts', acct, 'account.json')

    @classmethod
    def path_account_key( cls, acct ) : 
        return os.path.join(TestEnv.STORE_DIR, 'accounts', acct, 'account.key')

    @classmethod
    def authz_save( cls, name, content ) :
        dir = os.path.join(TestEnv.STORE_DIR, 'staging', name)
        os.makedirs(dir)
        open( os.path.join( dir, 'authz.json'), "w" ).write(content)

    @classmethod
    def path_domain_cert( cls, domain ) : 
        return os.path.join(TestEnv.STORE_DIR, 'domains', domain, 'cert.pem')

    @classmethod
    def path_domain_pkey( cls, domain ) : 
        return os.path.join(TestEnv.STORE_DIR, 'domains', domain, 'pkey.pem')

    @classmethod
    def path_domain_ca_chain( cls, domain ) : 
        return TestEnv.STORE_DIR + "/domains/" + domain + "/chain.pem"

    # --------- control apache ---------

    @classmethod
    def install_test_conf( cls, conf) :
        if conf is None:
            conf_src = os.path.join("conf", "test.conf")
        else:
            conf_src = os.path.join(cls.APACHE_CONF_SRC, conf + ".conf")
        copyfile(conf_src, cls.APACHE_TEST_CONF)
    
    @classmethod
    def apachectl( cls, conf, cmd ) :
        cls.install_test_conf(conf)
        return subprocess.call([cls.APACHECTL, "-d", cls.WEBROOT, "-k", cmd])

    @classmethod
    def apache_restart( cls ) :
        rv = subprocess.call([cls.APACHECTL, "-d", cls.WEBROOT, "-k", "graceful"])
        if rv == 0:
            rv = 0 if cls.is_live(cls.HTTPD_URL, 5) else -1
        return rv
        
    @classmethod
    def apache_start( cls ) :
        rv = subprocess.call([cls.APACHECTL, "-d", cls.WEBROOT, "-k", "start"])
        if rv == 0:
            rv = 0 if cls.is_live(cls.HTTPD_URL, 5) else -1
        return rv

    @classmethod
    def apache_stop( cls ) :
        rv = subprocess.call([cls.APACHECTL, "-d", cls.WEBROOT, "-k", "stop"])
        if rv == 0:
            rv = 0 if cls.is_dead(cls.HTTPD_URL, 5) else -1
        return rv

    @classmethod
    def apache_fail( cls ) :
        rv = 0 if subprocess.call([cls.APACHECTL, "-d", cls.WEBROOT, "-k", "graceful"]) != 0 else -1
        if rv == 0:
            print "check, if dead: " + cls.HTTPD_URL
            rv = 0 if cls.is_dead(cls.HTTPD_URL, 5) else -1
        return rv
        
    @classmethod
    def apache_err_reset( cls ):
        if os.path.isfile(cls.ERROR_LOG):
            os.remove(cls.ERROR_LOG)

    RE_MD_RESET = re.compile('.*\[md:info\].*initializing\.\.\.')
    RE_MD_ERROR = re.compile('.*\[md:error\].*')
    RE_MD_WARN  = re.compile('.*\[md:warn\].*')

    @classmethod
    def apache_err_count( cls ):
        if not os.path.isfile(cls.ERROR_LOG):
            return (0, 0)
        else:
            fin = open(cls.ERROR_LOG)
            ecount = 0
            wcount = 0
            for line in fin:
                m = cls.RE_MD_ERROR.match(line)
                if m:
                    ecount += 1
                    continue
                m = cls.RE_MD_WARN.match(line)
                if m:
                    wcount += 1
                    continue
                m = cls.RE_MD_RESET.match(line)
                if m:
                    ecount = 0
                    wcount = 0
            return (ecount, wcount)


# --------- certificate handling ---------


class CertUtil(object):
    # Utility class for inspecting certificates in test cases
    # Uses PyOpenSSL: https://pyopenssl.org/en/stable/index.html

    def __init__(self, cert_path):
        self.cert_path = cert_path
        # load certificate and private key
        if cert_path.startswith("http"):
            cert_data = TestEnv.get_plain(cert_path, 1)
        else:
            cert_data = CertUtil._load_binary_file(cert_path)

        for file_type in (OpenSSL.crypto.FILETYPE_PEM, OpenSSL.crypto.FILETYPE_ASN1):
            try:
                self.cert = OpenSSL.crypto.load_certificate(file_type, cert_data)
            except Exception as error:
                self.error = error

        if self.cert is None:
            raise self.error

    def get_serial(self):
        return self.cert.get_serial_number()

    def get_not_before(self):
        tsp = self.cert.get_notBefore()
        return self._parse_tsp(tsp)

    def get_not_after(self):
        tsp = self.cert.get_notAfter()
        return self._parse_tsp(tsp)

    def get_cn(self):
        return self.cert.get_subject().CN

    def get_san_list(self):
        text = OpenSSL.crypto.dump_certificate(OpenSSL.crypto.FILETYPE_TEXT, self.cert).decode("utf-8")
        m = re.search(r"X509v3 Subject Alternative Name:\s*(.*)", text)
        sans_list = []
        if m:
            sans_list = m.group(1).split(",")

        def _strip_prefix(s): return s.split(":")[1]  if  s.strip().startswith("DNS:")  else  s.strip()
        return map(_strip_prefix, sans_list)

    @classmethod
    def validate_privkey(cls, privkey_path):
        privkey_data = cls._load_binary_file(privkey_path)
        privkey = OpenSSL.crypto.load_privatekey(OpenSSL.crypto.FILETYPE_PEM, privkey_data)
        return privkey.check()

    def validate_cert_matches_priv_key(self, privkey_path):
        # Verifies that the private key and cert match.
        privkey_data = CertUtil._load_binary_file(privkey_path)
        privkey = OpenSSL.crypto.load_privatekey(OpenSSL.crypto.FILETYPE_PEM, privkey_data)
        context = OpenSSL.SSL.Context(OpenSSL.SSL.SSLv23_METHOD)
        context.use_privatekey(privkey)
        context.use_certificate(self.cert)
        context.check_privatekey()

    # --------- _utils_ ---------

    def _parse_tsp(self, tsp):
        # timestampss returned by PyOpenSSL are bytes
        # parse date and time part
        tsp_reformat = [tsp[0:4], b"-", tsp[4:6], b"-", tsp[6:8], b" ", tsp[8:10], b":", tsp[10:12], b":", tsp[12:14]]
        timestamp =  datetime.strptime(b"".join(tsp_reformat), '%Y-%m-%d %H:%M:%S')
        # adjust timezone
        tz_h, tz_m = 0, 0
        m = re.match(r"([+\-]\d{2})(\d{2})", b"".join([tsp[14:]]))
        if m:
            tz_h, tz_m = int(m.group(1)),  int(m.group(2))  if  tz_h > 0  else  -1 * int(m.group(2))
        return timestamp.replace(tzinfo = self.FixedOffset(60 * tz_h + tz_m))

    @classmethod
    def _load_binary_file(cls, path):
        with open(path, mode="rb")	 as file:
            return file.read()

    class FixedOffset(tzinfo):

        def __init__(self, offset):
            self.__offset = timedelta(minutes = offset)

        def utcoffset(self, dt):
            return self.__offset

        def tzname(self, dt):
            return None

        def dst(self, dt):
            return timedelta(0)
