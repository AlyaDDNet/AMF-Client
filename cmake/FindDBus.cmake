if(NOT CMAKE_CROSSCOMPILING)
  find_package(PkgConfig QUIET)
  pkg_check_modules(PC_DBUS QUIET dbus-1)
endif()

find_library(DBUS_LIBRARY
  NAMES dbus-1
  HINTS ${PC_DBUS_LIBDIR} ${PC_DBUS_LIBRARY_DIRS}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)
find_path(DBUS_INCLUDEDIR
  NAMES dbus/dbus.h
  HINTS ${PC_DBUS_INCLUDEDIR} ${PC_DBUS_INCLUDE_DIRS}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DBus DEFAULT_MSG DBUS_LIBRARY DBUS_INCLUDEDIR)

mark_as_advanced(DBUS_LIBRARY DBUS_INCLUDEDIR)

if(DBUS_FOUND)
  set(DBUS_LIBRARIES ${DBUS_LIBRARY})
  # dbus/dbus-arch-deps.h is architecture-specific and is not necessarily in
  # the same include directory as dbus/dbus.h.
  set(DBUS_INCLUDE_DIRS ${DBUS_INCLUDEDIR} ${PC_DBUS_INCLUDE_DIRS})
endif()
