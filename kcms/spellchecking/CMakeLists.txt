set(kcm_spellchecking_SRCS
   spellchecking.cpp
   )

add_library(kcmspellchecking MODULE ${kcm_spellchecking_SRCS})

target_link_libraries(kcmspellchecking
                      KF5::SonnetUi
                      KF5::KDELibs4Support
                     )

install(TARGETS kcmspellchecking   DESTINATION ${KDE_INSTALL_PLUGINDIR} )
install( FILES spellchecking.desktop   DESTINATION  ${KDE_INSTALL_KSERVICES5DIR} )
