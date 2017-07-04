# test mod_md acme terms-of-service handling

import json
import os
import re
import shutil
import sys
import time
import pytest

from datetime import datetime
from shutil import copyfile
from testbase import TestEnv

def setup_module(module):
    print("setup_module: %s" % module.__name__)
    TestEnv.init()

def teardown_module(module):
    print("teardown_module: %s" % module.__name__)


class TestRegAdd :

    def setup_method(self, method):
        print("setup_method: %s" % method.__name__)
        TestEnv.clear_store()

    def teardown_method(self, method):
        print("teardown_method: %s" % method.__name__)


    def test_100(self):
        # test case: list empty store
        jout = TestEnv.a2md( [ "list" ] )['jout']
        assert 'output' not in jout
        assert jout['status'] == 0

    def test_101(self):
        # test case: list two managed domains
        # setup: add managed domains
        dnslist = [ 
            [ "test-100.com", "test-101.com", "test-102.com" ],
            [ "greenbytes2.de", "www.greenbytes2.de", "mail.greenbytes2.de"]
        ]
        for dns in dnslist:
            assert TestEnv.a2md( [ "add" ] + dns )['rv'] == 0

        # list all store content
        jout = TestEnv.a2md( [ "list" ] )['jout']
        assert len(jout['output']) == len(dnslist)
        dnslist.reverse()
        for i in range (0, len(jout['output'])):
            assert jout['output'][i] == {
                "name": dnslist[i][0],
                "domains": dnslist[i],
                "contacts": [],
                "ca": {
                    "url": TestEnv.ACME_URL,
                    "proto": "ACME"
                },
                "state": TestEnv.MD_S_INCOMPLETE
            }
        # list md by name
        for dns in [ "test-100.com", "greenbytes2.de"]:
            md = TestEnv.a2md( [ "list", dns ] )['jout']['output'][0]
            assert md['name'] == dns

    def test_102(self):
        # test case: validate md state in store
        # check: md without pkey/cert -> INCOMPLETE
        name = "example.org"
        assert TestEnv.a2md(["add", name])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "contacts", "admin@" + name ])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "agreement", TestEnv.ACME_TOS ])['rv'] == 0
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_INCOMPLETE
        # check: valid pkey/cert -> COMPLETE
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_pkey.pem"), TestEnv.path_domain_pkey(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_cert(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_ca_chain(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_COMPLETE
        # check: expired cert -> EXPIRED
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "expired_pkey.pem"), TestEnv.path_domain_pkey(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "expired_cert.pem"), TestEnv.path_domain_cert(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "expired_cert.pem"), TestEnv.path_domain_ca_chain(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_EXPIRED

    def test_103(self):
        # test case: broken cert file
        #setup: prepare md in store
        name = "example.org"
        assert TestEnv.a2md(["add", name])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "contacts", "admin@" + name ])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "agreement", TestEnv.ACME_TOS ])['rv'] == 0
        # check: valid pkey/cert -> COMPLETE
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_pkey.pem"), TestEnv.path_domain_pkey(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_cert(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_ca_chain(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_COMPLETE
        # check: replace cert by broken file -> ERROR
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.req"), TestEnv.path_domain_cert(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_ERROR

    def test_104(self):
        # test case: broken private key file
        #setup: prepare md in store
        name = "example.org"
        assert TestEnv.a2md(["add", name])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "contacts", "admin@" + name ])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "agreement", TestEnv.ACME_TOS ])['rv'] == 0
        # check: valid pkey/cert -> COMPLETE
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_pkey.pem"), TestEnv.path_domain_pkey(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_cert(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_ca_chain(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_COMPLETE
        # check: replace private key by broken file -> ERROR
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.req"), TestEnv.path_domain_pkey(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_ERROR

    @pytest.mark.skip(reason="chain not yet part of state validation")
    def test_105(self):
        # test case: broken chain file
        #setup: prepare md in store
        name = "example.org"
        assert TestEnv.a2md(["add", name])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "contacts", "admin@" + name ])['rv'] == 0
        assert TestEnv.a2md([ "update", name, "agreement", TestEnv.ACME_TOS ])['rv'] == 0
        # check: valid pkey/cert -> COMPLETE
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_pkey.pem"), TestEnv.path_domain_pkey(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_cert(name))
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.pem"), TestEnv.path_domain_ca_chain(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_COMPLETE
        # check: replace chain by broken file -> ERROR
        copyfile(os.path.join(TestEnv.TESTROOT, "data", "ssl", "valid_cert.req"), TestEnv.path_domain_ca_chain(name))
        assert TestEnv.a2md([ "list", name ])['jout']['output'][0]['state'] == TestEnv.MD_S_ERROR
