Summary: HP Visualization Stack
Name: vizstack-opensg-demos
Version: 0.9
Release: 0
License: GPLV2
Group: Development/Tools
URL: http://www.hp.com/go/visualization
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
This contains the OpenSG demos for VizStack

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
/opt/vizstack/share/demos/
/opt/vizstack/share/demos/OpenSG
/opt/vizstack/share/demos/OpenSG/bin
/opt/vizstack/share/demos/OpenSG/bin/*
/opt/vizstack/share/demos/OpenSG/src
/opt/vizstack/share/demos/OpenSG/src/*
/opt/vizstack/share/demos/OpenSG/data
/opt/vizstack/share/demos/OpenSG/data/*
%doc

%post

%changelog
* Wed Jul 15 2009 Shree Kumar <shree.kumar@hp.com> - 
- Improved window size to 800x600, so that people
  don't ignore the windows!

* Fri Jun 30 2009 Shree Kumar <shree.kumar@hp.com> - 
- Bumped up version to meet vizstack number. Will
  keep up this madness for ease of use.
  
* Fri Jun 29 2009 Shree Kumar <shree.kumar@hp.com> - 
- Initial build.

