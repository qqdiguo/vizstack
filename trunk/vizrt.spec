Summary: VizStack Remote Access Tools
Name: vizrt
Version: 1.0
Release: 0
License: GPLV2
Group: Applications/Internet
URL: http://vizstack.sourceforge.net
BuildArch: noarch
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires: wxPython paramiko

%description

%prep
%setup -q

%build

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
cp -r opt $RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
/opt/vizrt/bin/*
%doc


%changelog
* Thu Sep 17 2009 manjunath.sripadarao <manjunath.sripadarao@hp.com> - 
- Initial build.

