// --- sélecteur d'affichage pour l'impression ---
// 1 = base du boîtier seul, 2 = couvercle seul, 3 = les deux pièces
choix_piece = 2; 

// --- paramètres du boîtier (dimensions internes basées sur tes mesures) ---
longueur_interne = 60;
largeur_interne  = 33;
hauteur_interne  = 16;
epaisseur_paroi  = 2;
jeu              = 0.4; // espace pour insérer facilement la carte
rayon_boitier    = 3;   // rayon de l'arrondi des coins externes

// --- paramètres des bandes latérales internes (bords gauche et droit) ---
bande_largeur   = 2; // largeur de la bande à l'intérieur
bande_epaisseur = 3; // épaisseur (hauteur) de la bande depuis le fond

// --- paramètres de la découpe du clavier ---
clavier_longueur = 0;
clavier_largeur  = 0;
clavier_epaisseur= 1; // profondeur du renfoncement sur le couvercle

// --- calculs des dimensions externes ---
longueur_externe = longueur_interne + (2 * epaisseur_paroi) + jeu;
largeur_externe  = largeur_interne + (2 * epaisseur_paroi) + jeu;
hauteur_boite    = hauteur_interne + epaisseur_paroi;

$fn = 64; // lissage des arrondis

// --- logique d'affichage selon le choix de l'option ---
if (choix_piece == 1) {
    base_boitier();
}
else if (choix_piece == 2) {
    // positionné directement à plat en z=0 si imprimé seul
    translate([0, 0, epaisseur_paroi]) 
        rotate([0, 180, 0]) 
        couvercle();
}
else if (choix_piece == 3) {
    translate([0, 0, 0]) base_boitier();
    
    // décalé sur l'axe y pour ne pas chevaucher la base
    translate([longueur_externe, largeur_externe + 10, epaisseur_paroi]) 
        rotate([0, 180, 0]) 
        couvercle();
}

// --- module d'aide : cube avec coins verticaux arrondis ---
module cube_arrondi(x, y, z, r) {
    translate([r, r, 0])
    minkowski() {
        cube([x - 2*r, y - 2*r, z - 0.1]);
        cylinder(r = r, h = 0.1);
    }
}

// --- module : base du boîtier ---
module base_boitier() {
    difference() {
        // volume externe de la boîte avec coins arrondis
        cube_arrondi(longueur_externe, largeur_externe, hauteur_boite, rayon_boitier);
        
        // évidement interne principal pour le nodemcu
        translate([epaisseur_paroi, epaisseur_paroi, epaisseur_paroi])
            cube([longueur_interne + jeu, largeur_interne + jeu, hauteur_boite]);
        
        // ouverture frontale pour l'accès au port usb-c
        translate([-1, (largeur_externe / 2) - 6, epaisseur_paroi])
            cube([epaisseur_paroi + 2, 12, 7]);
        
        // encoche fine à l'arrière pour faire passer la nappe plate du clavier
        translate([longueur_externe - epaisseur_paroi - 1, (largeur_externe / 2) - 7, hauteur_boite - 1.5])
            cube([epaisseur_paroi + 2, 14, 2]);
    }
    
    // ajout des deux bandes de maintien sur toute la longueur interne
    // elles sont ajoutées après la "difference" pour ne pas être creusées
    
    // bande latérale gauche (le long du bord inférieur y)
    translate([epaisseur_paroi, epaisseur_paroi, epaisseur_paroi])
        cube([longueur_interne + jeu, bande_largeur, bande_epaisseur]);
        
    // bande latérale droite (le long du bord supérieur y)
    translate([epaisseur_paroi, largeur_externe - epaisseur_paroi - bande_largeur, epaisseur_paroi])
        cube([longueur_interne + jeu, bande_largeur, bande_epaisseur]);
}

// --- module : couvercle avec emplacement clavier ---
module couvercle() {
    difference() {
        union() {
            // plaque supérieure externe du couvercle avec les mêmes coins arrondis
            cube_arrondi(longueur_externe, largeur_externe, epaisseur_paroi, rayon_boitier);
            
            // lèvre interne droite pour emboîter et maintenir le couvercle en place
            translate([epaisseur_paroi + (jeu/2), epaisseur_paroi + (jeu/2), -epaisseur_paroi])
                cube([longueur_interne, largeur_interne, epaisseur_paroi]);
        }
        
        // évidement interne de la lèvre pour économiser du filament et laisser de la place
        translate([epaisseur_paroi * 2, epaisseur_paroi * 2, -epaisseur_paroi - 1])
            cube([longueur_interne - (epaisseur_paroi * 2), largeur_interne - (epaisseur_paroi * 2), epaisseur_paroi + 2]);
            
        // renfoncement sur le dessus pour intégrer le clavier à fleur du couvercle
        translate([(longueur_externe - clavier_longueur) / 2, (largeur_externe - clavier_largeur) / 2, epaisseur_paroi - clavier_epaisseur])
            cube([clavier_longueur, clavier_largeur, clavier_epaisseur + 1]);
            
        // fente débouchante au bout du renfoncement pour glisser la nappe à l'intérieur du boîtier
        translate([longueur_externe - epaisseur_paroi - 6, (largeur_externe - 12) / 2, -epaisseur_paroi - 2])
            cube([4, 12, hauteur_boite]);
    }
}
