import { Component } from '@angular/core';

import { Dashboard } from './dashboard/dashboard';

@Component({
  imports: [Dashboard],
  selector: 'gscope-root',
  template: '<gscope-dashboard />',
})
export class App {}
