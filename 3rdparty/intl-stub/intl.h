#ifndef _LIBINTL_H
#define _LIBINTL_H

#define _(String) (String)
#define gettext(String) (String)
#define bindtextdomain(Domain, Directory) ((void)0)
#define textdomain(Domain) ((void)0)

#endif
