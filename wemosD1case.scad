// --- sélecteur d'affichage ---
choix_piece = 3; 

// --- inserts M2 (définir en premier car rayon_boitier en dépend) ---
insert_plot      = 9.0;   // diamètre colonne pour insert M2 — à ajuster
insert_diam_perc = 3.1;
insert_profondeur= 8.0;
vis_diam_passage = 2.4;

// --- paramètres du boîtier ---
longueur_interne = 60;
largeur_interne  = 46;
hauteur_interne  = 8.5;
epaisseur_paroi  = 2;
jeu              = 0.5; 
rayon_boitier    = insert_plot / 2;  // coin arrondi = rayon du plot

// --- barre de stop ---
wemos_longueur   = 34.5;
stop_largeur     = 3;
stop_hauteur     = 4;

// --- bandes latérales ---
bande_largeur   = 2; 
bande_epaisseur = 2; 

// --- clavier ---
clavier_longueur = 0;
clavier_largeur  = 0;
clavier_epaisseur= 0; 

// --- dimensions externes ---
longueur_externe = longueur_interne + (2 * epaisseur_paroi) + jeu;
largeur_externe  = largeur_interne  + (2 * epaisseur_paroi) + jeu;
hauteur_boite    = hauteur_interne  + epaisseur_paroi;

// Centres = centres des arrondis de coin
col_x_usb     = rayon_boitier;
col_x_clavier = longueur_externe - rayon_boitier;
col_y_bas     = rayon_boitier;
col_y_haut    = largeur_externe - rayon_boitier;

positions_colonnes = [
    [col_x_usb,     col_y_bas ],
    [col_x_usb,     col_y_haut],
    [col_x_clavier, col_y_bas ],
    [col_x_clavier, col_y_haut]
];

$fn = 64; 

// --- affichage ---
if (choix_piece == 1)      { base_boitier(); }
else if (choix_piece == 2) { translate([0, 0, epaisseur_paroi]) rotate([0, 180, 0]) couvercle(); }
else if (choix_piece == 3) {
    base_boitier();
    translate([longueur_externe, largeur_externe + 10, epaisseur_paroi]) 
        rotate([0, 180, 0]) couvercle();
}

// --- cube coins arrondis ---
module cube_arrondi(x, y, z, r) {
    translate([r, r, 0])
    minkowski() {
        cube([x - 2*r, y - 2*r, z - 0.1]);
        cylinder(r = r, h = 0.1);
    }
}

// --- base ---
module base_boitier() {
    union() {
        difference() {
            cube_arrondi(longueur_externe, largeur_externe, hauteur_boite, rayon_boitier);

            // évidement interne
            translate([epaisseur_paroi, epaisseur_paroi, epaisseur_paroi])
                cube([longueur_interne + jeu, largeur_interne + jeu, hauteur_boite]);

            // ouverture USB
            translate([-1, (largeur_externe / 2) - 6, epaisseur_paroi])
                cube([epaisseur_paroi + 2, 12, 5]);
            
            // fente nappe
            translate([longueur_externe - epaisseur_paroi - 1, (largeur_externe / 2) - 7, hauteur_boite - 1.5])
                cube([epaisseur_paroi + 2, 14, 2]);
        }

        // barre de stop
        translate([epaisseur_paroi + wemos_longueur + (jeu/2), epaisseur_paroi, epaisseur_paroi])
            cube([stop_largeur, largeur_interne + jeu, stop_hauteur]);

        // colonnes PLEINES — perçage soustrait séparément
        for (pos = positions_colonnes) {
            difference() {
                translate([pos[0], pos[1], epaisseur_paroi])
                    cylinder(d = insert_plot, h = hauteur_interne);
                translate([pos[0], pos[1], epaisseur_paroi - 0.1])
                    cylinder(d = insert_diam_perc, h = insert_profondeur + 0.1);
            }
        }
    }
}

// --- couvercle ---
module couvercle() {
    difference() {
        cube_arrondi(longueur_externe, largeur_externe, epaisseur_paroi, rayon_boitier);

        // fente nappe clavier
        translate([longueur_externe - epaisseur_paroi - 6, (largeur_externe - 12) / 2, -0.1])
            cube([4, 12, epaisseur_paroi + 0.2]);

        // trous de passage vis M2
        for (pos = positions_colonnes) {
            translate([pos[0], pos[1], -0.1])
                cylinder(d = vis_diam_passage, h = epaisseur_paroi + 0.2);
        }
    }
}
