Summary: Software to convert one/more machines with GPUs into a sharable, multi-user, multi-session visualization resource.
Name: vizstack
Version: 0.9
Release: 1
License: GPLV2
Group: Development/Tools
URL: http://vizstack.sourceforge.net
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
VizStack is a software stack that turns a one or more machines with GPUs installed in them into a shared, multi-user visualization resource.  VizStack provides utilities to allocate resources (GPUs), run applications on them, and free them when they are no longer needed.  VizStack provides ways to configure and drive display devices, as well as higher level constructs like Tiled Displays. 

VizStack manages only the visualization resources (GPUs, X servers), and does not provide any utilities to do system management/setup on the nodes. VizStack can dynamically setup the visualization resources to meet any application requirement.

For ease of use, VizStack provides integrations with HP Remote Graphics Software and TurboVNC/VirtualGL, as well as popular visualization applications.

%prep
%setup -q

%build


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
cp -r opt usr etc lib64 $RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
/opt/vizstack/python/*
/opt/vizstack/README
/opt/vizstack/COPYING
/opt/vizstack/bin/*
/opt/vizstack/sbin/*
/opt/vizstack/share/*
/opt/vizstack/src/*
/usr/X11R6/bin/vs-X
/etc/vizstack
/etc/profile.d/vizstack.csh
/etc/profile.d/vizstack.sh
/lib64/security/pam_vizstack_rgs_setuser.so
%doc
/opt/vizstack/man/man1/*

%post
chmod +s /usr/X11R6/bin/vs-X
chmod +s /opt/vizstack/bin/vs-GDMlauncher
chmod +s /opt/vizstack/bin/vs-Xkill
mkdir -p /var/run/vizstack

%changelog
* Mon Oct 13 2009 Shree Kumar <shreekumar@hp.com>
- Included vizstack source in vizstack package
* Wed Sep 30 2009 Shree Kumar <shreekumar@hp.com>
- Bumped version to 0.9-1
   - added vs-test-gpus
   - improved the manual to include references to
     several tools
   - fixed an X server shutdown/startup bug

* Wed Sep 24 2009 Shree Kumar <shreekumar@hp.com>
- Bumped version to 0.9-0, to aim for a 1.0 release
   - SSM is now a daemon
   - scripts now renamed to "viz-" rather than viz_
   - New tools for managing tiled display, enumerating jobs and killing jobs
   - Dynamic reload of tiled displays
   - --server option for Paraview script
   - Manpages for many tools.
   - Reformatted & enlarged documentation in AsciiDoc.
   - Automatic framelock in scripts

* Fri Aug 28 2009 Shree Kumar <shreekumar@hp.com>
- Bumped version to 0.4-0
   - viz_ls added by Manju
   - SLI, SLI mosaic. But no support in tiled displays yet.
   - Framelock. Not integrated in scripts yet.
   - VizStack Remote Access GUI. Windows packaging implemented.

* Wed Jul 21 2009 Shree Kumar <shreekumar@hp.com>
- Bumped version to 0.3-3
  New funtionality
   - viz_rgs_multi, copied from viz_rgs. The difference is that this does not use :0
   - PAM module needed for viz_rgs_multi, needs to be enabled explicitly.
   - Not intended for release yet. A few more changes are needed before that happens.

* Wed Jul 15 2009 Shree Kumar <shreekumar@hp.com>
- Bumped version to 0.3-2
  New funtionality
   - viz_desktop
   - viz_rgs works with Tiled Displays
   - viz_vgl - use VirtualGL directly without TurboVNC
   - many fixes (including one which prevented multiple mice from being used)
   - viz_paraview works both with in sort-first & sort-last

* Tue Jul 07 2009 Shree Kumar <shreekumar@hp.com>
- Bumped version to 0.3-1
  Fixed many bugs compared to earlier release.
   - Panning domain
   - Xinerama
  Implemented
   - group_blocks
   - enhancements to viz_rgs and viz_tvnc to make
     them more usable
   - support for S series devices, not comprehensive
* Wed Jun 30 2009 Shree Kumar <shreekumar@hp.com>
- Upped version to 0.3-0.
  Many changes since earlier release
   - Integrated TurboVNC closer
   - Added support for display rotation
   - Added support for port remapping in tiled
     display. This will be useful for workstations,
     as well as for switching left & right in stereo!
   - More sample programs ?
   - panning domains supported as well.

* Wed Jun 29 2009 Shree Kumar <shreekumar@hp.com>
- Bumped up version number to 0.2-4. Significant changes
  since 0.2-2 :
   - working RGS
   - more reliable TurboVNC
   - many samples
   - keyboard/mouse detection in configuration script
   - display rotation
   - templates in /opt/vizstack/share

* Wed Jun 17 2009 Shree Kumar <shreekumar@hp.com>
- Bumped up version number to 0.2, before releasing it to C&I,
  competency center & Glenn.

* Mon Jun  8 2009 Manjunath Sripadarao <manjunath.sripadarao@hp.com>
- Added samples directory to /opt/vizstack/etc with some sample config files.

* Fri May 29 2009 Manjunath Sripadarao <manjunath.sripadarao@hp.com> - 
- Initial build.

