# test mod_md basic configurations

import json
import os
import pytest
import re
import socket
import ssl
import sys
import time

from datetime import datetime
from httplib import HTTPSConnection
from test_base import TestEnv
from test_base import HttpdConf
from test_base import CertUtil

def setup_module(module):
    print("setup_module    module:%s" % module.__name__)
    TestEnv.init()
    TestEnv.APACHE_CONF_SRC = "data/test_auto"
    
def teardown_module(module):
    print("teardown_module module:%s" % module.__name__)
    assert TestEnv.apache_stop() == 0


class TestAuto:


    @classmethod
    def setup_class(cls):
        cls.dns_uniq = "%d.org" % time.time()
        cls.TMP_CONF = os.path.join(TestEnv.GEN_DIR, "auto.conf")

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        TestEnv.check_acme()
        TestEnv.clear_store()
        TestEnv.install_test_conf();
        assert TestEnv.apache_start() == 0


    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)
        assert TestEnv.apache_stop() == 0


    def test_700_001(self):
        # create a MD not used in any virtual host
        # auto drive should NOT pick it up
        domain = "a001-" + TestAuto.dns_uniq
        dnsList = [ domain, "www." + domain ]

        # - generate config with one md
        conf = HttpdConf(TestAuto.TMP_CONF)
        conf.add_admin("admin@" + domain)
        conf.add_drive_mode("auto")
        conf.add_md(dnsList)
        conf.install()

        # - restart (-> drive), check that md is in store
        assert TestEnv.apache_restart() == 0
        self._check_md_names(domain, dnsList)
        self._wait_for_state_change([ domain ], 30)
        self._check_md_cert(dnsList)
        
        # - add vhost for the md, restart and check access
        conf.add_vhost(TestEnv.HTTPS_PORT, domain, aliasList=[ dnsList[1] ], withSSL=True)
        conf.install()
        assert TestEnv.apache_restart() == 0
        assert TestEnv.checkCertAltName(domain)

        # check: challenges removed
        TestEnv.check_dir_empty( TestEnv.path_challenges() )

        # check file system permissions:
        md = TestEnv.a2md([ "list", domain ])['jout']['output'][0]
        TestEnv.check_file_access( TestEnv.path_store_json(), 0600 )
        # domains
        TestEnv.check_file_access( os.path.join( TestEnv.STORE_DIR, 'domains' ), 0700 )
        TestEnv.check_file_access( os.path.join( TestEnv.STORE_DIR, 'domains', domain ), 0700 )
        TestEnv.check_file_access( TestEnv.path_domain_pkey( domain ), 0600 )
        TestEnv.check_file_access( TestEnv.path_domain_cert( domain ), 0600 )
        TestEnv.check_file_access( TestEnv.path_domain_ca_chain( domain ), 0600 )
        TestEnv.check_file_access( TestEnv.path_domain( domain ), 0600 )
        # archive
        TestEnv.check_file_access( TestEnv.path_domain( domain, archiveVersion=1 ), 0600 )
        # accounts
        acc = md['ca']['account']
        TestEnv.check_file_access( os.path.join( TestEnv.STORE_DIR, 'accounts' ), 0755 )
        TestEnv.check_file_access( os.path.join( TestEnv.STORE_DIR, 'accounts', acc ), 0755 )
        TestEnv.check_file_access( TestEnv.path_account( acc ), 0644 )
        TestEnv.check_file_access( TestEnv.path_account_key( acc ), 0644 )
        # staging
        TestEnv.check_file_access( os.path.join( TestEnv.STORE_DIR, 'staging' ), 0755 )

    def test_700_002(self):
        # test case: same as test_100, but with two parallel managed domains
        domainA = "a002a-" + TestAuto.dns_uniq
        domainB = "a002b-" + TestAuto.dns_uniq
        # - generate config with one md
        dnsListA = [ domainA, "www." + domainA ]
        dnsListB = [ domainB, "www." + domainB ]

        conf = HttpdConf(TestAuto.TMP_CONF)
        conf.add_admin("admin@example.org")
        conf.add_drive_mode("auto")
        conf.add_md(dnsListA)
        conf.add_md(dnsListB)
        conf.add_vhost(TestEnv.HTTPS_PORT, domainA, aliasList=[ dnsListA[1] ], withSSL=True)
        conf.add_vhost(TestEnv.HTTPS_PORT, domainB, aliasList=[ dnsListB[1] ], withSSL=True)
        conf.install()

        # - restart, check that md is in store
        assert TestEnv.apache_restart() == 0
        self._check_md_names(domainA, dnsListA)
        self._check_md_names(domainB, dnsListB)
        # - drive
        self._wait_for_state_change([ domainA, domainB ], 30)
        self._check_md_cert(dnsListA)
        self._check_md_cert(dnsListB)

        # check: SSL is running OK
        assert TestEnv.checkCertAltName(domainA)
        assert TestEnv.checkCertAltName(domainB)

    def test_700_003(self):
        # test case: one md, that covers two vhosts
        domain = "a003-" + TestAuto.dns_uniq
        nameA = "test-a." + domain
        nameB = "test-b." + domain
        dnsList = [ domain, nameA, nameB ]

        # - generate config with one md
        conf = HttpdConf(TestAuto.TMP_CONF)
        conf.add_admin("admin@" + domain)
        conf.add_md(dnsList)
        conf.add_vhost(TestEnv.HTTPS_PORT, nameA, aliasList=[], docRoot="htdocs/a", 
                       withSSL=True, certPath=TestEnv.path_domain_cert(domain), 
                       keyPath=TestEnv.path_domain_pkey(domain))
        conf.add_vhost(TestEnv.HTTPS_PORT, nameB, aliasList=[], docRoot="htdocs/b", 
                       withSSL=True, certPath=TestEnv.path_domain_cert(domain), 
                       keyPath=TestEnv.path_domain_pkey(domain))
        conf.install()

        # - create docRoot folder
        self._write_res_file(os.path.join(TestEnv.APACHE_HTDOCS_DIR, "a"), "name.txt", nameA)
        self._write_res_file(os.path.join(TestEnv.APACHE_HTDOCS_DIR, "b"), "name.txt", nameB)

        # - restart, check that md is in store
        assert TestEnv.apache_restart() == 0
        self._check_md_names(domain, dnsList)
        # - drive
        self._wait_for_state_change([ domain ], 30)
        self._check_md_cert(dnsList)

        # check: SSL is running OK
        assert TestEnv.checkCertAltName(nameA)
        assert TestEnv.checkContent(nameA, "/name.txt", nameA)
        assert TestEnv.checkCertAltName(nameB)
        assert TestEnv.checkContent(nameB, "/name.txt", nameB)

    def test_700_004(self):
        # test case: httpd only allows HTTPS -> drive uses TLS-SNI challenge
        domain = "a004-" + TestAuto.dns_uniq
        dnsList = [ domain, "www." + domain ]

        # setup: generate config with one md, one vhost
        assert TestEnv.apache_stop() == 0
        conf = HttpdConf(TestAuto.TMP_CONF, sslOnly=True)
        conf.add_admin("admin@" + domain)
        conf.add_drive_mode("auto")
        conf.add_md(dnsList)
        conf.add_vhost(TestEnv.HTTPS_PORT, domain, aliasList=[ dnsList[1] ], withSSL=True)
        conf.install()

        # - restart (-> drive), check that md is in store
        assert TestEnv.apache_restart( checkWithSSL=True ) == 0
        self._check_md_names(domain, dnsList)
        self._wait_for_state_change([ domain ], 30)
        self._check_md_cert(dnsList)
        
        # - check access
        TestEnv.checkCertAltName(domain)

    def test_700_005(self):
        # test case: drive_mode manual, check that server starts,
        # but requests do domain are denied
        domain = "a005-" + TestAuto.dns_uniq
        nameA = "test-a." + domain
        dnsList = [ domain, nameA ]

        # - generate config with one md
        conf = HttpdConf(TestAuto.TMP_CONF)
        conf.add_admin("admin@" + domain)
        conf.add_drive_mode("manual")
        conf.add_md(dnsList)
        conf.add_vhost(TestEnv.HTTPS_PORT, nameA, aliasList=[], docRoot="htdocs/a", 
                       withSSL=True, certPath=TestEnv.path_domain_cert(domain), 
                       keyPath=TestEnv.path_domain_pkey(domain))
        conf.install()

        # - create docRoot folder
        self._write_res_file(os.path.join(TestEnv.APACHE_HTDOCS_DIR, "a"), "name.txt", nameA)

        # - restart, check that md is in store
        assert TestEnv.apache_restart() == 0
        self._check_md_names(domain, dnsList)
        # - drive

        # check: that request to domains give 503 Service Unavailable
        TestEnv.checkCertAltName(nameA)
        test_url_a = "https://%s:%s/name.txt" % (nameA, TestEnv.HTTPS_PORT)
        dnsResolveA = "%s:%s:127.0.0.1" % (nameA, TestEnv.HTTPS_PORT)
        result = TestEnv.curl([ "--resolve", dnsResolveA, 
                              "-k", "-D", "-", test_url_a])
        assert result['rv'] == 0
        assert re.match("HTTP/\\d(.\\d)? 503 .*", result['stdout'])


    # --------- _utils_ ---------

    def _write_res_file(self, docRoot, name, content):
        if not os.path.exists(docRoot):
            os.makedirs(docRoot)
        open(os.path.join(docRoot, name), "w").write(content)

    def _check_md_names(self, name, dnsList):
        md = TestEnv.a2md([ "-j", "list", name ])['jout']['output'][0]
        assert md['name'] == name
        assert md['domains'] == dnsList

    def _check_md_cert(self, dnsList):
        name = dnsList[0]
        md = TestEnv.a2md([ "list", name ])['jout']['output'][0]
        # check tos agreement, cert url
        assert md['state'] == TestEnv.MD_S_COMPLETE
        assert "url" in md['cert']
        assert os.path.isfile( TestEnv.path_domain_pkey(name) )
        assert os.path.isfile( TestEnv.path_domain_cert(name) )

    def _wait_for_state_change(self, nameList, timeout):
        prevState = []
        for name in nameList:
            prevState.append( TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] )
        try_until = time.time() + timeout

        while time.time() < try_until:
            time.sleep(1)
            allChanged = True
            for i in range(0, len(nameList)):
                state = TestEnv.a2md([ "list", name ])['jout']['output'][0]['state']
                allChanged = allChanged and state != prevState[i]
            if allChanged:
                return
        pytest.fail('timeout exceeded')
