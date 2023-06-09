// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Mixin for scrollable containers with <iron-list>.
 *
 * Any containers with the 'scrollable' attribute set will have the following
 * classes toggled appropriately: can-scroll, is-scrolled, scrolled-to-bottom.
 * These classes are used to style the container div and list elements
 * appropriately, see cr_shared_style.css.
 *
 * The associated HTML should look something like:
 *   <div id="container" scrollable>
 *     <iron-list items="[[items]]" scroll-target="container">
 *       <template>
 *         <my-element item="[[item]] tabindex$="[[tabIndex]]"></my-element>
 *       </template>
 *     </iron-list>
 *   </div>
 *
 * In order to get correct keyboard focus (tab) behavior within the list,
 * any elements with tabbable sub-elements also need to set tabindex, e.g:
 *
 * <dom-module id="my-element>
 *   <template>
 *     ...
 *     <paper-icon-button toggles active="{{opened}}" tabindex$="[[tabindex]]">
 *   </template>
 * </dom-module>
 *
 * NOTE: If 'container' is not fixed size, it is important to call
 * updateScrollableContents() when [[items]] changes, otherwise the container
 * will not be sized correctly.
 */

// clang-format off
import {beforeNextRender, dedupingMixin, microTask, PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {IronListElement} from 'chrome://resources/polymer/v3_0/iron-list/iron-list.js';
import {assert} from 'chrome://resources/js/assert_ts.js';
// clang-format on

type IronListElementWithExtras = IronListElement&{
  savedScrollTops: number[],
};

type Constructor<T> = new (...args: any[]) => T;

export const CrScrollableMixin = dedupingMixin(
    <T extends Constructor<PolymerElement>>(superClass: T): T&
    Constructor<CrScrollableMixinInterface> => {
      class CrScrollableMixin extends superClass implements
          CrScrollableMixinInterface {
        private intervalId_: number|null = null;

        override ready() {
          super.ready();

          beforeNextRender(this, () => {
            this.requestUpdateScroll();

            // Listen to the 'scroll' event for each scrollable container.
            const scrollableElements =
                this.shadowRoot!.querySelectorAll('[scrollable]');
            for (const scrollableElement of scrollableElements) {
              scrollableElement.addEventListener(
                  'scroll', this.updateScrollEvent_.bind(this));
            }
          });
        }

        override disconnectedCallback() {
          super.disconnectedCallback();

          if (this.intervalId_ !== null) {
            clearInterval(this.intervalId_);
          }
        }

        /**
         * Called any time the contents of a scrollable container may have
         * changed. This ensures that the <iron-list> contents of dynamically
         * sized containers are resized correctly.
         */
        updateScrollableContents() {
          if (this.intervalId_ !== null) {
            return;
          }  // notifyResize is already in progress.

          this.requestUpdateScroll();

          const nodeList = this.shadowRoot!.querySelectorAll<IronListElement>(
              '[scrollable] iron-list');
          if (!nodeList.length) {
            return;
          }

          interface NodeToResize {
            node: IronListElement;
            lastScrollHeight: number;
          }

          let nodesToResize: NodeToResize[] =
              Array.from(nodeList).map(node => ({
                                         node: node,
                                         lastScrollHeight: 0,
                                       }));
          // Use setInterval to avoid initial render / sizing issues.
          this.intervalId_ = window.setInterval(() => {
            const checkAgain: NodeToResize[] = [];
            nodesToResize.forEach(({node, lastScrollHeight}) => {
              const scrollHeight = node.parentElement!.scrollHeight;
              // A hidden scroll-container has a height of 0. When not hidden,
              // it has a min-height of 1px and the iron-list needs a resize to
              // show the initial items and update the |scrollHeight|. The
              // initial item count is determined by the |scrollHeight|. A
              // scrollHeight of 1px will result in the minimum default item
              // count (currently 3). After the |scrollHeight| is updated to be
              // greater than 1px, another resize is needed to correctly
              // calculate the number of physical iron-list items to render.
              if (scrollHeight !== lastScrollHeight) {
                const ironList = node as IronListElement;
                ironList.notifyResize();
              }

              // TODO(crbug.com/1121679): Add UI Test for this behavior.
              if (scrollHeight <= 1 &&
                  window.getComputedStyle(node.parentElement!).display !==
                      'none') {
                checkAgain.push({
                  node: node,
                  lastScrollHeight: scrollHeight,
                });
              }
            });
            if (checkAgain.length === 0) {
              assert(this.intervalId_);
              window.clearInterval(this.intervalId_);
              this.intervalId_ = null;
            } else {
              nodesToResize = checkAgain;
            }
          }, 10);
        }

        /**
         * Setup the initial scrolling related classes for each scrollable
         * container. Called from ready() and updateScrollableContents(). May
         * also be called directly when the contents change (e.g. when not using
         * iron-list).
         */
        requestUpdateScroll() {
          requestAnimationFrame(() => {
            const scrollableElements =
                this.shadowRoot!.querySelectorAll<HTMLElement>('[scrollable]');
            for (const scrollableElement of scrollableElements) {
              this.updateScroll_(scrollableElement);
            }
          });
        }

        saveScroll(list: IronListElementWithExtras) {
          // Store a FIFO of saved scroll positions so that multiple updates in
          // a frame are applied correctly. Specifically we need to track when
          // '0' is saved (but not apply it), and still handle patterns like
          // [30, 0, 32].
          list.savedScrollTops = list.savedScrollTops || [];
          list.savedScrollTops.push(list.scrollTarget!.scrollTop);
        }

        restoreScroll(list: IronListElementWithExtras) {
          microTask.run(() => {
            const scrollTop = list.savedScrollTops.shift();
            // Ignore scrollTop of 0 in case it was intermittent (we do not need
            // to explicitly scroll to 0).
            if (scrollTop !== 0) {
              list.scroll(0, scrollTop!);
            }
          });
        }

        /**
         * Event wrapper for updateScroll_.
         */
        private updateScrollEvent_(event: Event) {
          const scrollable = event.target as HTMLElement;
          this.updateScroll_(scrollable);
        }

        /**
         * This gets called once initially and any time a scrollable container
         * scrolls.
         */
        private updateScroll_(scrollable: HTMLElement) {
          scrollable.classList.toggle(
              'can-scroll', scrollable.clientHeight < scrollable.scrollHeight);
          scrollable.classList.toggle('is-scrolled', scrollable.scrollTop > 0);
          scrollable.classList.toggle(
              'scrolled-to-bottom',
              scrollable.scrollTop + scrollable.clientHeight >=
                  scrollable.scrollHeight);
        }
      }
      return CrScrollableMixin;
    });

export interface CrScrollableMixinInterface {
  updateScrollableContents(): void;
  requestUpdateScroll(): void;
  saveScroll(list: IronListElement): void;
  restoreScroll(list: IronListElement): void;
}
