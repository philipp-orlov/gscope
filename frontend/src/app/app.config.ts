import { provideHttpClient } from '@angular/common/http';
import { ApplicationConfig, provideBrowserGlobalErrorListeners } from '@angular/core';

// No @angular/animations/provideAnimationsAsync(): Material 3's theming is
// CSS-driven, and the schematic itself doesn't wire it in for a fresh
// project -- pulling in the (now deprecated) animations engine here would
// only add weight without changing how anything looks.
export const appConfig: ApplicationConfig = {
  providers: [provideBrowserGlobalErrorListeners(), provideHttpClient()],
};
