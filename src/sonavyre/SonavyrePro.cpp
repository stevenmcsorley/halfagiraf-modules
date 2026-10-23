#include "ui/Interface.hpp"
struct SonavyreProWidget : SonavyreWidget {
 SonavyreProWidget(SonavyreEngine* m) : SonavyreWidget(m,true) {}
};
Model* modelSonavyrePro = createModel<SonavyreEngine, SonavyreProWidget>("SonavyrePro");
