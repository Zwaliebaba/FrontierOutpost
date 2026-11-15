/**
 * Which of the station's two views the home draws (design §11), and what its control says.
 * The exterior is the station as it stands at the dock; selecting it opens the interior's cutaway
 * in the same camera, where building, upgrading and reconfiguring happen. So the facilities
 * service, which builds, is the interior's: opening it opens the interior, and leaving the
 * interior leaves it. The other services work from either view.
 */
import type { HomeTab } from "./ui";

export type StationView = "exterior" | "interior";

export interface HomeView {
  view: StationView;
  tab: HomeTab;
}

export const HOME_VIEW: HomeView = { view: "exterior", tab: "overview" };

/** A service chosen: the facilities service opens the interior. */
export function openTab(state: HomeView, tab: HomeTab): HomeView {
  return { view: tab === "facilities" ? "interior" : state.view, tab };
}

/** The station selected, or a socket: the interior opens on the facilities service. */
export function openInterior(): HomeView {
  return { view: "interior", tab: "facilities" };
}

/** Back to the exterior; the facilities service closes with the interior. */
export function leaveInterior(state: HomeView): HomeView {
  return { view: "exterior", tab: state.tab === "facilities" ? "overview" : state.tab };
}

/** What the view's control says and does. */
export interface ViewControl {
  /** Which view is drawn, in the HUD's caption. */
  caption: string;
  /** What the control does when pressed. */
  action: "interior" | "exterior";
  /** Its label. */
  label: string;
  /** A line on what else does the same. */
  hint: string;
}

export function viewControl(view: StationView): ViewControl {
  return view === "interior"
    ? {
        caption: "STATION / INTERIOR CUTAWAY",
        action: "exterior",
        label: "Exterior",
        hint: "Esc returns to the exterior",
      }
    : {
        caption: "STATION / EXTERIOR",
        action: "interior",
        label: "Open interior",
        hint: "Select the station to open it",
      };
}
