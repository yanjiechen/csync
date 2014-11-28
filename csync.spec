Summary: sohu content sync tool
Name: csync
Version: 1.4.3
Release: 20100624.1.as5
Vendor: SOHU INC.
Copyright: Commercial
Group: Application/Internet
Source0: %{name}-%{version}-%{release}.tar.gz
BuildRoot: %{_builddir}/%{name}-root 
Packager: Chen Yanjie <yanjiechen@sohu-inc.com>
URL: http://mm.no.sohu.com/csync/%{name}-%{version}-%{release}.i386.rpm
%description
Content sync tool is written for SOHU CMS group. it's a tool that send
the newly updated html/pic files to real servers in the IDC.

%undefine __check_files

%prep
# prep start
%setup -q -n %name
#-%{version}
#%setup

%build
# build start
ulimit -HSn 65535
if [ "`uname -m`" = "x86_64" ]; then
	mv -f Makefile_x8664 Makefile
	mv -f lib64/rotatelogs bin/rotatelogs
fi
make
strip csync
mv csync bin

%install
# install start
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/arc
mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/bin
mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/doc
mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/etc
mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/log
mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/run
mkdir -p $RPM_BUILD_ROOT/opt/itc/csync/src

mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p $RPM_BUILD_ROOT/etc/logrotate.d
mkdir -p $RPM_BUILD_ROOT/var/run/csync

install -m755 bin/csync $RPM_BUILD_ROOT/opt/itc/csync/bin
install -m755 bin/csyncwatchdog.sh $RPM_BUILD_ROOT/opt/itc/csync/bin
install -m755 bin/rotatelogs $RPM_BUILD_ROOT/opt/itc/csync/bin
install -m755 init.d/csync $RPM_BUILD_ROOT/etc/rc.d/init.d
install -m644 logrotate.d/csync $RPM_BUILD_ROOT/etc/logrotate.d
install -m644 etc/* $RPM_BUILD_ROOT/opt/itc/csync/etc
install -m644 README $RPM_BUILD_ROOT/opt/itc/csync

%clean
rm -rf $RPM_BUILD_ROOT 

%pre
# Move config files to safety
if test -f /opt/itc/csync/etc/csync.conf; then 
  if grep -q "/var/run/csync.pid" /opt/itc/csync/etc/csync.conf; then
		perl -pi -e 's@/var/run/csync.pid@/var/run/csync/csync.pid@g' /opt/itc/csync/etc/csync.conf
  fi
  #mv -f /opt/itc/csync/etc/csync.conf /opt/itc/csync/etc/csync.conf.rpmtemp;
fi

if [ -f /var/run/csync/csync.pid ]; then
	echo -n "Shutting down csync: "
	kill -TERM `cat /var/run/csync/csync.pid`
	RC=$?
	[ $RC -eq 0 ] && echo -e "\E[32mOK\E[0m" || echo -e "\E[31mERROR\E[0m"
	sleep 1
	rm -f /var/lock/subsys/csync
fi


%post
# Move config files back and new ones to a different name
#if test -f /opt/itc/csync/etc/csync.conf.rpmtemp; then
#  if test -f /opt/itc/csync/etc/csync.conf; then
#    mv -f /opt/itc/csync/etc/csync.conf /opt/itc/csync/etc/csync.conf.rpmnew;
#  fi
#  mv -f /opt/itc/csync/etc/csync.conf.rpmtemp /opt/itc/csync/etc/csync.conf;
#fi
/sbin/chkconfig --add csync >/dev/null 2>&1

# add csyncwatchdog.sh to crontab (2005-04-15 14:28)
crontab -l |grep -v csyncwatchdog.sh > /tmp/csync.crontab.list
echo "*/3 * * * * /opt/itc/csync/bin/csyncwatchdog.sh >/dev/null 2>/dev/null" >> /tmp/csync.crontab.list
crontab /tmp/csync.crontab.list

%preun
/etc/rc.d/init.d/csync stop >/dev/null 2>&1
/sbin/chkconfig --del csync >/dev/null 2>&1

%postun

%files
%defattr (-,root,root)
%config(noreplace) /opt/itc/csync/etc/csync.conf
%config(noreplace) /opt/itc/csync/etc/server.conf
%config(noreplace) /opt/itc/csync/etc/promisc.conf
%config(noreplace) /opt/itc/csync/etc/client.conf

/etc/rc.d/init.d/csync
/opt/itc/csync
/var/run/csync
/etc/logrotate.d

#%define date    %(echo `LC_ALL="C" date +"%a %b %d %Y"`)