# -*- rd -*-

= Install to CentOS --- How to install milter manager to CentOS

== About this document

This document describes how to install milter manager to
CentOS. See ((<Install|install.rd>)) for general install
information.

We assume that CentOS version is 5.2. We also assume that we
use sudo to run a command with root authority. If you don't
use sudo, use su instead.

== Install packages

To install the following packages, related packages are also
installed:

  % sudo yum install -y libtool intltool glib2-devel ruby-devel

We use Sendmail as MTA because it's installed by default.

We use spamass-milter, clamav-milter and milter-greylist as
milters. We use milter packages registered in RPMforge.

We register RPMforge like the following.

On 32bit environment:
  % wget http://packages.sw.be/rpmforge-release/rpmforge-release-0.3.6-1.el5.rf.i386.rpm
  % sudo rpm -Uhv rpmforge-release-0.3.6-1.el5.rf.i386.rpm

On 64bit environment:
  % wget http://packages.sw.be/rpmforge-release/rpmforge-release-0.3.6-1.el5.rf.x86_64.rpm
  % sudo rpm -Uhv rpmforge-release-0.3.6-1.el5.rf.x86_64.rpm

Now, we can install milters:

  % sudo yum install -y spamass-milter clamav-milter milter-greylist

== Build and Install

  % mkdir -p ~/src/

=== Install Ruby/GLib2

We use Ruby/GLib2 0.16.0 because Ruby/GLib2 0.17 or later
doesn't support Ruby 1.8.5.

  % cd ~/src/
  % wget http://downloads.sourceforge.net/ruby-gnome2/ruby-gtk2-0.16.0.tar.gz
  % tar xvzf ruby-gtk2-0.16.0.tar.gz
  % cd ruby-gtk2-0.16.0
  % ruby extconf.rb glib
  % make
  % sudo make install

=== Install milter manager

We install milter manager under /usr/local/.

  % cd ~/src/
  % wget http://downloads.sourceforge.net/milter-manager/milter-manager-0.9.0.tar.gz
  % tar xvzf milter-manager-0.9.0.tar.gz
  % cd milter-manager-0.9.0
  % ./configure
  % make
  % sudo make install

We create a user for milter-manager.

  % sudo /usr/sbin/groupadd -r milter-manager
  % sudo /usr/sbin/useradd -r -s /sbin/nologin -c 'milter manager' -g milter-manager milter-manager

== Configuration

Here is a basic configuration policy.

We use IPv4 socket and accepts connections only from localhost.

milter-greylist should be applied only if
((<S25R|URL:http://gabacho.reto.jp/en/anti-spam/>))
condition is matched to reduce needless delivery delay.
But the configuration is automatically done by
milter-manager. We need to do nothing for it.

=== Configure spamass-milter

At first, we configure spamd.

spamd adds "[SPAM]" to spam mail's subject by default. If
you don't like the behavior, edit
/etc/main/spamassassin/local.cf.

Before:
  rewrite_header Subject [SPAM]

After:
  # rewrite_header Subject [SPAM]

We add the following configuration to
/etc/spamassassin/local.cf. This configuration is for adding
headers only if spam detected.

  remove_header ham Status
  remove_header ham Level

We start spamd on startup:

  % sudo /sbin/chkconfig spamassassin on

We start spamd:

  % sudo /sbin/service spamassassin start

We change spamass-milter's socket address. We append the
following link to /etc/sysconfig/spamass-milter:

  SOCKET="inet:11120@[127.0.0.1]"

We start spamass-milter on startup:

  % sudo /sbin/chkconfig spamass-milter on

We start spamass-milter:

  % sudo /sbin/service spamass-milter start

=== Configure clamav-milter

We start clamd.

  % sudo /sbin/service clamd start

We edit /etc/sysconfig/clamav-milter to change
clamav-milter's socket address and disable Sendmail
configuration file check.

Before:
  SOCKET_ADDRESS="local:/var/clamav/clmilter.socket"

After:
  SOCKET_ADDRESS="inet:11121@[127.0.0.1]"
  CLAMAV_FLAGS="$CLAMAV_FLAGS --no-check-cf"

We start clamav-milter:

  % sudo /sbin/service clamav-milter start

=== Configure milter-greylist

We edit /etc/mail/greylist.conf to use Greylist by default.

Before:
  racl whitelist default

After:
  racl greylist default

We create /etc/sysconfig/milter-greylist with the following
content:

  OPTIONS="$OPTIONS -p inet:11122@[127.0.0.1]"

We start milter-greylist on startup:

  % sudo /sbin/chkconfig milter-greylist on

We start milter-greylist:

  % sudo /sbin/service milter-greylist start

=== Configure milter-manager

milter-manager detects milters that installed in system.
We can confirm spamass-milter, clamav-milter and
milter-greylist are detected:

  % /usr/local/sbin/milter-manager --show-config

The following output shows milters are detected:

  ...
  define_milter("milter-greylist") do |milter|
    milter.connection_spec = "inet:11122@[127.0.0.1]"
    ...
    milter.enabled = true
    ...
  end
  ...
  define_milter("clamav-milter") do |milter|
    milter.connection_spec = "inet:11121@[127.0.0.1]"
    ...
    milter.enabled = true
    ...
  end
  ...
  define_milter("spamass-milter") do |milter|
    milter.connection_spec = "inet:11120@[127.0.0.1]"
    ...
    milter.enabled = true
    ...
  end
  ...

We should confirm that milter's name, socket path and
'enabled = true'. If the values are unexpected,
we need to change
/usr/local/etc/milter-manager/milter-manager.conf.
See ((<Configuration|configuration.rd>)) for details of
milter-manager.conf.

But if we can, we want to use milter manager without editing
miter-manager.conf. If you report your environment to the
milter manager project, the milter manager project may
improve detect method.

milter-manager saves its process ID to
/var/run/milter-manager/milter-manager.pid by default on
CentOS. We need to create /var/run/milter-manager/ directory
before running milter-manager:

  % sudo mkdir -p /var/run/milter-manager
  % sudo chown -R milter-manager:milter-manager /var/run/milter-manager

milter-manager's configuration is finished. We start to
configure about milter-manager's start-up.

milter-manager has its own run script for CentOS. It will be
installed into
/usr/local/etc/milter-manager/init.d/redhat/milter-manager.
We need to create a symbolic link to /etc/init.d/ and
mark it run on start-up:

  % cd /etc/init.d
  % sudo ln -s /usr/local/etc/milter-manager/init.d/redhat/milter-manager ./
  % sudo /sbin/chkconfig --add milter-manager

The run script assumes that milter-manager command is
installed into /usr/sbin/milter-manager. We need to create a
symbolik link:

  % cd /usr/sbin
  % sudo ln -s /usr/local/sbin/milter-manager ./

We start milter-manager:

  % sudo /sbin/service milter-manager start

/usr/local/bin/milter-test-server is usuful to confirm
milter-manager was ran:

  % sudo -u milter-manager milter-test-server -s unix:/var/run/milter-manager/milter-manager.sock

Here is a sample success output:

  status: pass
  elapsed-time: 0.128 seconds

If milter-manager fails to run, the following message will
be shown:

  Failed to connect to unix:/var/run/milter-manager/milter-manager.sock

In this case, we can use log to solve the
problem. milter-manager is verbosily if --verbose option is
specified. milter-manager outputs logs to standard output if
milter-manager isn't daemon process.

We can add the following configuration to
/etc/sysconfig/milter-manager to output verbose log to
standard output:

  OPTION_ARGS="--verbose --no-daemon"

We start milter-manager again:

  % sudo /sbin/service milter-manager start

Some logs are output if there is a problem. Running
milter-manager can be exitted by Ctrl+c.

OPTION_ARGS configuration in /etc/sysconfig/milter-manager
should be commented out after the problem is solved to run
milter-manager as daemon process. And we should restart
milter-manager.

=== Configure Sendmail

First, we enables Sendmail:

  % sudo /sbin/chkconfig --add sendmail
  % sudo /sbin/service sendmail start

We append the following content to /etc/mail/sendmail.mc to
register milter-manager to Sendmail.

  INPUT_MAIL_FILTER(`milter-manager', `S=local:/var/run/milter-manager/milter-manager.sock')

It's important that spamass-milter, clamav-milter,
milter-greylist aren't needed to be registered because they
are used via milter-manager.

We update Sendmail configuration and reload it.

  % sudo make -C /etc/mail
  % sudo /sbin/service sendmail reload

Sendmail's milter configuration is completed.

milter-manager logs to syslog. If milter-manager works well,
some logs can be showen in /var/log/maillog. We need to sent
a test mail for confirming.

== Conclusion

There are many configurations to work milter and Sendmail
together. They can be reduced by introducing milter-manager.

Without milter-manager, we need to specify sockets of
spamass-milter, clamav-milter and milter-greylist to
sendmail.mc. With milter-manager, we doesn't need to
specify sockets of them, just specify a socket of
milter-manager. They are detected automatically. We doesn't
need to take care some small mistakes like typo.

milter-manager also detects which '/sbin/chkconfig -add' is
done or not. If we disable a milter, we use the following
steps:

  % sudo /sbin/service milter-greylist stop
  % sudo /sbin/chkconfig --del milter-greylist

We need to reload milter-manager after we disable a milter.

  % sudo /sbin/service milter-manager reload

milter-manager detects a milter is disabled and doesn't use
it. We doesn't need to change Sendmail's sendmail.mc.

We can reduce maintainance cost by introducing
milter-manager if we use some milters on CentOS.
