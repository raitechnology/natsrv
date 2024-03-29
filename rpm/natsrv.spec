Name:		natsrv
Version:	999.999
Vendor:	        Rai Technology, Inc
Release:	99999%{?dist}
Summary:	NATS RV Bridge

License:	ASL 2.0
URL:		https://github.com/raitechnology/%{name}
Source0:	%{name}-%{version}-99999.tar.gz
BuildRoot:	${_tmppath}
Prefix:	        /usr
BuildRequires:  gcc-c++
BuildRequires:  chrpath
BuildRequires:  raikv
BuildRequires:  raimd
BuildRequires:  libdecnumber
BuildRequires:  pcre2-devel
BuildRequires:  hdrhist
BuildRequires:  natsmd
BuildRequires:  sassrv
BuildRequires:  git-core
Requires:       raikv
Requires:       raimd
Requires:       libdecnumber
Requires:       pcre2
Requires:       natsmd
Requires:       sassrv

%description
Rai NATS RV Bridge

%prep
%setup -q


%define _unpackaged_files_terminate_build 0
%define _missing_doc_files_terminate_build 0
%define _missing_build_ids_terminate_build 0
%define _include_gdb_index 1

%build
make build_dir=./usr %{?_smp_mflags} dist_bins
cp -a ./include ./usr/include

%install
rm -rf %{buildroot}
mkdir -p  %{buildroot}

# in builddir
cp -a * %{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/*

%post

%postun

%changelog
* __DATE__ <support@raitechnology.com>
- Hello world
