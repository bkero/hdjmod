
Summary: Hercules DJ Series Kernel Module
Name: hdjmod-dkms
Version: %{version}
Release: 1
License: GPLv2+
Group: System Environment/Kernel
URL: http://hercules.com
Source: hdjmod-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{module}-%{version}
BuildArch: noarch
Requires: gcc, make
Requires(post): dkms
Requires(preun): dkms

%description
This is the Hercules DJ Series Kernel Module, which supports Hercules DJ Devices.

%prep
%setup -n hdjmod-%{version}

%build

%install
%{__rm} -rf %{buildroot}

%define dkms_name hdjmod
%define dkms_vers %{version}
%define quiet -q

# Kernel module sources install for dkms
%{__mkdir_p} %{buildroot}%{_usrsrc}/%{dkms_name}-%{dkms_vers}/
%{__cp} -a * %{buildroot}%{_usrsrc}/%{dkms_name}-%{dkms_vers}/

# Configuration for dkms
%{__cat} > %{buildroot}%{_usrsrc}/%{dkms_name}-%{dkms_vers}/dkms.conf << 'EOF'
PACKAGE_NAME=%{dkms_name}
PACKAGE_VERSION=%{dkms_vers}
BUILT_MODULE[0]="hdj_mod"
BUILT_MODULE_NAME[0]="hdj_mod"
DEST_MODULE_LOCATION[0]="/kernel/sound/usb"
REMAKE_INITRD="no"
AUTOINSTALL="YES"
POST_INSTALL="hdj_mod_post_install"
POST_REMOVE="hdj_mod_post_remove"
EOF


%clean
%{__rm} -rf %{buildroot}


%post
# Add to DKMS registry
dkms add -m %{dkms_name} -v %{dkms_vers} %{?quiet} || :
# Rebuild and make available for the currenty running kernel
dkms build -m %{dkms_name} -v %{dkms_vers} %{?quiet} || :
dkms install -m %{dkms_name} -v %{dkms_vers} %{?quiet} --force || :

%preun
# Remove all versions from DKMS registry
dkms remove -m %{dkms_name} -v %{dkms_vers} %{?quiet} --all || :
module_version=0
module_version2=%{dkms_vers}
target_install_dir="/usr/share/hercules-hdj_mod"
udev_install_dir="/etc/udev/rules.d"
module_version_file="module_version"
module_version=`cat $target_install_dir/$module_version_file | tee true`
module_version2 = %{dkms_vers}
rmmod hdj_mod > /dev/null 2>&1 | true
modprobe hdj_mod > /dev/null 2>&1 | true
if [ "$module_version" = "$module_version2" ]; then
	rm $udev_install_dir/98-hdj.rules | true
        rm -rf $target_install_dir | true
fi

%files
%defattr(-, root, root, 0755)
%{_usrsrc}/%{dkms_name}-%{dkms_vers}/


%changelog
* Tue Jan 27 2009 Philip Lukidis
- Fixed 64 bit compilation issue.
* Tue Jan 13 2009 Philip Lukidis
- Initial RPM release.
