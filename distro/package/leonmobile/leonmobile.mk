################################################################################
#
# leonmobile
#
################################################################################

# Product aggregation owns no executable or SDK files.
LEONMOBILE_VERSION = 1.0.0
LEONMOBILE_SOURCE =
LEONMOBILE_LICENSE = N/A
LEONMOBILE_DEPENDENCIES = leon-core lmui leon-desktop leon-apps

# Preserve the established developer command while rebuilding every local unit.
.PHONY: leonmobile-rebuild leonmobile-reconfigure
leonmobile-rebuild: leon-core-rebuild lmui-rebuild .WAIT leon-desktop-rebuild leon-apps-rebuild
leonmobile-reconfigure: leon-core-reconfigure lmui-reconfigure .WAIT leon-desktop-reconfigure leon-apps-reconfigure

# Expand the product targets after the component prerequisites. Otherwise the
# aggregator could visit components before their rebuild stamps are cleared.
$(eval $(generic-package))
