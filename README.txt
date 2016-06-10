##
##
## do the following commands to generate a postscript of this thesis template
latex cuthesis
dvips <cuthesis.dvi >cuthesis.ps
##
##
##When you have added a bibliography section to your thesis the command to 
##use are
latex cuthesis
bibtex cuthesis
latex cuthesis
dvips <cuthesis.dvi >cuthesis.ps
